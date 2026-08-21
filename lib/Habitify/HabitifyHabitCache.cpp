#include "HabitifyHabitCache.h"

#include <Logging.h>

#include <algorithm>
#include <utility>

void HabitifyHabitCache::toJson(JsonDocument& doc) const {
  char iso[11];
  civil::isoFromDate(syncDate, iso, sizeof(iso));
  doc["date"] = iso;

  JsonArray arr = doc["habits"].to<JsonArray>();
  for (const auto& habit : habits) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = habit.id;
    obj["name"] = habit.name;
    obj["unit"] = habit.unitSymbol;
    obj["current"] = habit.current;
    obj["target"] = habit.target;
    // Written even when zero: a press made with the radio off has to survive a
    // reboot, which is the whole point of queueing it.
    obj["pending"] = habit.pending;
  }
}

bool HabitifyHabitCache::fromJson(JsonVariantConst doc) {
  habits.clear();
  syncDate = civil::dateFromIso(doc["date"] | "");

  JsonArrayConst arr = doc["habits"].as<JsonArrayConst>();
  habits.reserve(std::min(arr.size(), MAX_HABITS));
  for (JsonObjectConst obj : arr) {
    if (habits.size() >= MAX_HABITS) break;
    HabitifyHabit habit;
    habit.id = obj["id"] | "";
    // No id means nothing can be logged against it and pending progress could
    // never be matched back, so it is not a habit as far as this screen is
    // concerned.
    if (habit.id.empty()) continue;
    habit.name = obj["name"] | "";
    habit.unitSymbol = obj["unit"] | "";
    habit.current = obj["current"] | 0.0f;
    habit.target = obj["target"] | 0.0f;
    habit.pending = obj["pending"] | 0.0f;
    if (habit.pending < 0.0f) habit.pending = 0.0f;
    habits.push_back(std::move(habit));
  }

  LOG_DBG("HHC", "Loaded %zu habits, %zu with unpushed progress", habits.size(), pendingCount());
  return true;
}

void HabitifyHabitCache::setHabits(std::vector<HabitifyHabit>&& fetched, const uint16_t date) {
  if (fetched.size() > MAX_HABITS) fetched.resize(MAX_HABITS);

  // Pending progress is carried across by id. The fetch knows nothing about it -
  // it reports what the server holds - so taking its result wholesale would drop
  // any press made between the push and the re-fetch.
  for (auto& habit : fetched) {
    const auto previous =
        std::find_if(habits.begin(), habits.end(), [&habit](const HabitifyHabit& held) { return held.id == habit.id; });
    if (previous != habits.end()) habit.pending = previous->pending;
  }

  habits = std::move(fetched);
  if (date != civil::NO_DATE) syncDate = date;
}

void HabitifyHabitCache::addPending(const size_t index, const float amount) {
  if (index >= habits.size() || amount <= 0.0f) return;
  habits[index].pending += amount;
}

void HabitifyHabitCache::clearPending(const std::string& habitId, const float pushed) {
  const auto found =
      std::find_if(habits.begin(), habits.end(), [&habitId](const HabitifyHabit& h) { return h.id == habitId; });
  if (found == habits.end()) return;
  found->pending -= pushed;
  // Subtracting rather than zeroing leaves anything added while the request was
  // in flight still owed; clamped because the arithmetic is in floats.
  if (found->pending < 0.0f) found->pending = 0.0f;
}

bool HabitifyHabitCache::hasPending() const {
  return std::any_of(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.hasPending(); });
}

size_t HabitifyHabitCache::pendingCount() const {
  return static_cast<size_t>(
      std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.hasPending(); }));
}

void HabitifyHabitCache::clear() {
  habits.clear();
  syncDate = civil::NO_DATE;
}
