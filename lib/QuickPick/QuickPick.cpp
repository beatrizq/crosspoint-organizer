#include "QuickPick.h"

#include <numeric>

namespace quickpick {

uint32_t totalWeight(const std::vector<WeightedItem>& items) {
  return std::accumulate(items.begin(), items.end(), uint32_t{0},
                         [](const uint32_t sum, const WeightedItem& item) { return sum + item.weight; });
}

int pick(const std::vector<WeightedItem>& items, const uint32_t draw) {
  if (items.empty()) return -1;

  uint32_t cumulative = 0;
  for (size_t i = 0; i < items.size(); i++) {
    cumulative += items[i].weight;
    if (draw < cumulative) return static_cast<int>(i);
  }
  return static_cast<int>(items.size() - 1);
}

uint32_t habitWeight(const float current, const float target) {
  if (target <= 0.0f) return 1;

  float fraction = current / target;  // 0 = untouched, 1 = at (or past) target
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;

  // Scaled so a habit right at the start (fraction 0) sits at the same
  // baseline weight as a task, and one on the doorstep of done (fraction
  // close to 1) is up to five times as likely -- a real nudge without
  // drowning out the rest of the pool.
  constexpr float SCALE = 4.0f;
  return 1 + static_cast<uint32_t>(SCALE * fraction);
}

}  // namespace quickpick
