#include "HabitifyHabitCache.h"

#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
// A-Z by name, case-insensitive, matching the order Habitify's own app shows.
bool byNameCaseInsensitive(const HabitifyHabit& a, const HabitifyHabit& b) {
  return std::lexicographical_compare(
      a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
      [](const unsigned char l, const unsigned char r) { return std::tolower(l) < std::tolower(r); });
}
}  // namespace

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
    obj["areaId"] = habit.areaId;
    obj["current"] = habit.current;
    obj["target"] = habit.target;
    // Written even when zero: a press made with the radio off has to survive a
    // reboot, which is the whole point of queueing it.
    obj["pending"] = habit.pending;
    obj["completedByStatus"] = habit.completedByStatus;
    obj["pendingComplete"] = habit.pendingComplete;
  }

  JsonArray areaArr = doc["areas"].to<JsonArray>();
  for (const auto& area : areas) {
    JsonObject obj = areaArr.add<JsonObject>();
    obj["id"] = area.id;
    obj["name"] = area.name;
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
    habit.areaId = obj["areaId"] | "";
    habit.current = obj["current"] | 0.0f;
    habit.target = obj["target"] | 0.0f;
    habit.pending = obj["pending"] | 0.0f;
    if (habit.pending < 0.0f) habit.pending = 0.0f;
    habit.completedByStatus = obj["completedByStatus"] | false;
    habit.pendingComplete = obj["pendingComplete"] | false;
    habits.push_back(std::move(habit));
  }
  // A cache saved before A-Z ordering existed may not be sorted yet.
  std::sort(habits.begin(), habits.end(), byNameCaseInsensitive);

  areas.clear();
  JsonArrayConst areaArr = doc["areas"].as<JsonArrayConst>();
  areas.reserve(std::min(areaArr.size(), HABITIFY_MAX_AREAS));
  for (JsonObjectConst obj : areaArr) {
    if (areas.size() >= HABITIFY_MAX_AREAS) break;
    HabitifyArea area;
    area.id = obj["id"] | "";
    if (area.id.empty()) continue;
    area.name = obj["name"] | "";
    areas.push_back(std::move(area));
  }

  LOG_DBG("HHC", "Loaded %zu habits, %zu with unpushed progress, %zu areas", habits.size(), pendingCount(),
          areas.size());
  return true;
}

void HabitifyHabitCache::setHabits(std::vector<HabitifyHabit>&& fetched, const uint16_t date, const bool areasFresh,
                                   std::vector<HabitifyArea>&& fetchedAreas) {
  if (fetched.size() > MAX_HABITS) fetched.resize(MAX_HABITS);

  // Pending progress and pending completes are both carried across by id. The
  // fetch knows nothing about either - it reports what the server holds - so
  // taking its result wholesale would drop a press made between the push and
  // the re-fetch. areaId is carried across the same way, but only when this
  // sync's areas fetch failed -- see setHabits()'s own doc comment.
  for (auto& habit : fetched) {
    const auto previous =
        std::find_if(habits.begin(), habits.end(), [&habit](const HabitifyHabit& held) { return held.id == habit.id; });
    if (previous != habits.end()) {
      habit.pending = previous->pending;
      habit.pendingComplete = previous->pendingComplete;
      if (!areasFresh) habit.areaId = previous->areaId;
    }
  }

  std::sort(fetched.begin(), fetched.end(), byNameCaseInsensitive);
  habits = std::move(fetched);
  if (date != civil::NO_DATE) syncDate = date;

  if (areasFresh) {
    if (fetchedAreas.size() > HABITIFY_MAX_AREAS) fetchedAreas.resize(HABITIFY_MAX_AREAS);
    areas = std::move(fetchedAreas);
  }
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

void HabitifyHabitCache::completeHabitAt(const size_t index) {
  if (index >= habits.size()) return;
  habits[index].pendingComplete = true;
  // Optimistic: the push has not happened yet, but the user just said this is
  // done, and isComplete() already treats completedByStatus as authoritative -
  // the next sync's real fetch confirms it either way.
  habits[index].completedByStatus = true;
}

void HabitifyHabitCache::clearPendingComplete(const std::string& habitId) {
  const auto found =
      std::find_if(habits.begin(), habits.end(), [&habitId](const HabitifyHabit& h) { return h.id == habitId; });
  if (found == habits.end()) return;
  found->pendingComplete = false;
}

void HabitifyHabitCache::clear() {
  habits.clear();
  areas.clear();
  syncDate = civil::NO_DATE;
}
