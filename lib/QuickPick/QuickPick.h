#pragma once

#include <cstdint>
#include <vector>

// Weighted-random selection of one pending task or habit, for a "pick
// something for me" companion interaction. Deliberately free of
// Todoist/Habitify/Arduino types and any real RNG: the caller resolves its
// own tasks and habits into plain WeightedItems and supplies the random
// draw itself, so the whole policy is exercised by host unit tests.
namespace quickpick {

struct WeightedItem {
  // Opaque to this module: the caller's own way of finding the item again
  // (an index into its task or habit list, and which list it came from).
  int sourceIndex = 0;
  bool isHabit = false;
  // Relative selection weight. Must be >= 1 -- a 0-weight item can never be
  // drawn and would silently waste part of the draw range, so callers should
  // omit an item rather than pass 0 for it.
  uint32_t weight = 1;
};

// Total weight across every candidate, for sizing the random draw.
uint32_t totalWeight(const std::vector<WeightedItem>& items);

// Picks one item given a draw in [0, totalWeight(items)). Returns -1 when
// items is empty; a draw at or past the total is clamped onto the last item
// rather than treated as invalid, so a caller's `draw % total` off-by-one
// degrades gracefully instead of picking nothing.
int pick(const std::vector<WeightedItem>& items, uint32_t draw);

// A habit's weight toward being picked: higher the closer it is to its
// target, so a habit that is nearly done -- genuinely quick to finish -- is
// more likely to come up than one just started. There is no equivalent
// signal for tasks (Todoist has no size/effort field), so tasks are left at
// the default weight of 1 by the caller; this is the only source of bias in
// the whole scheme, and it is a real "how much is left" figure rather than
// an invented one.
uint32_t habitWeight(float current, float target);

}  // namespace quickpick
