#include "CompanionTracker.h"

#include <HabitifyHabitCache.h>
#include <HalClock.h>
#include <Logging.h>
#include <TodoistTaskCache.h>

#include <algorithm>

#include "CompanionState.h"
#include "CrossPointSettings.h"

namespace {
// clockUtcOffsetQ is biased by 48 so it fits in a uint8_t (48 == UTC+0).
int32_t signedUtcOffsetQuarterHours() {
  uint8_t biased = SETTINGS.clockUtcOffsetQ;
  if (biased > 104) biased = 104;  // guard a corrupted persisted value
  return static_cast<int32_t>(biased) - 48;
}
}  // namespace

bool CompanionTracker::isEnabled() { return SETTINGS.companionEnabled != 0; }

companion::CompanionId CompanionTracker::activeId() {
  const uint8_t id = SETTINGS.companionId;
  if (id >= companion::COMPANION_COUNT) return static_cast<companion::CompanionId>(0);
  return static_cast<companion::CompanionId>(id);
}

bool CompanionTracker::resolveLocalDay(int32_t& outDay) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;

  if (!halClock.getUtcDateTime(year, month, day, hour, minute)) return false;
  outDay = companion::localDayNumber(year, month, day, hour, minute, signedUtcOffsetQuarterHours());
  return true;
}

void CompanionTracker::refreshDay() {
  int32_t day = 0;
  if (!resolveLocalDay(day)) {
    clockValid = false;
    return;
  }
  clockValid = true;
  localDay = day;
}

void CompanionTracker::refreshForDisplay() {
  if (!isEnabled()) return;
  refreshDay();
}

uint16_t CompanionTracker::liveHabitsCompletedToday() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  return static_cast<uint16_t>(
      std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.isComplete(); }));
}

void CompanionTracker::recordActivity() {
  if (!isEnabled()) return;
  // The day can have rolled over since this screen was entered (reading past
  // midnight is a reader concern, but a completion right after waking the
  // device is not), so re-resolve it before crediting.
  refreshDay();

  const uint16_t tasksToday = TODOIST_TASKS.getCompletedToday();
  const uint16_t habitsToday = liveHabitsCompletedToday();

  if (!COMPANION_STATE.recordActivity(localDay, clockValid, tasksToday, habitsToday)) return;
  if (!COMPANION_STATE.saveToFile()) {
    LOG_ERR("COMP", "Failed to save companion state");
  }
}

companion::MoodInput CompanionTracker::buildMoodInput() const {
  const uint16_t tasksToday = TODOIST_TASKS.getCompletedToday();
  const uint16_t habitsToday = liveHabitsCompletedToday();
  return companion::moodInputFor(COMPANION_STATE.ledger, localDay, clockValid, tasksToday, habitsToday);
}

companion::Mood CompanionTracker::currentMood() const { return companion::evaluate(buildMoodInput()); }

uint16_t CompanionTracker::pointsToday() const {
  const auto in = buildMoodInput();
  const uint32_t total = static_cast<uint32_t>(in.tasksCompletedToday) + in.habitsCompletedToday;
  return static_cast<uint16_t>(total > UINT16_MAX ? UINT16_MAX : total);
}
