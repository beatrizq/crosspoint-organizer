#pragma once

#include <cstdint>

// Tasks-and-habits-driven mood model for the companion. Deliberately free of
// Arduino, FreeRTOS, and HAL includes so the whole decay/credit policy is
// exercised by host unit tests before it ever reaches the device.
namespace companion {

enum class Mood : uint8_t { Thriving = 0, Content = 1, Peckish = 2, Neglected = 3 };

// Tunables kept in one struct so tests can pin behaviour without rebuilding the
// firmware defaults. Tasks and habits are weighted identically -- each
// completed task or completed habit is worth one point -- so either can carry
// the ladder to Thriving on its own, or the two can mix freely.
struct MoodThresholds {
  uint16_t thrivingPoints = 3;  // combined tasks+habits completed today for Thriving
  uint16_t contentPoints = 1;   // combined tasks+habits completed today to count as "did something"
  uint8_t neglectedDays = 3;    // quiet days at or above which the mood bottoms out
};

struct MoodInput {
  uint16_t tasksCompletedToday = 0;
  uint16_t habitsCompletedToday = 0;
  // Whole calendar days between the last day that qualified (cleared
  // contentPoints) and today. 0 = qualified today, 1 = qualified yesterday, 2
  // = skipped a full day.
  uint16_t daysSinceLastActive = 0;
  // False when the RTC is absent or was never set (see HalClock::getDate).
  // Day arithmetic is meaningless then, so the decay ladder is skipped.
  bool clockValid = false;
};

// Maps today's organizing activity onto one of the four drawn poses.
//
// With a valid clock the ladder is: enough combined points today -> Thriving,
// some activity today or yesterday -> Content, one skipped day -> Peckish, and
// neglectedDays or more -> Neglected.
//
// Without a clock, elapsed days cannot be measured, so neglect is unknowable
// and the result never falls below Content. Today's task/habit counts are
// live reads regardless of clock validity, so Thriving stays reachable.
Mood evaluate(const MoodInput& in, const MoodThresholds& t = {});

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Integer-only: no <ctime>, no 64-bit division, and valid
// well past any date an e-reader RTC will report.
int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day);

// Whole days from the first date to the second. Negative when the second date
// is earlier, which the caller should treat as a clock that moved backwards.
int32_t daysBetween(int32_t fromYear, uint32_t fromMonth, uint32_t fromDay, int32_t toYear, uint32_t toMonth,
                    uint32_t toDay);

// Local day number (days since 1970-01-01 in the user's own zone) for a UTC
// wall-clock reading. The RTC stores UTC, but a day must roll over at the
// user's midnight, not UTC's -- otherwise a late-evening completion west of
// Greenwich lands on tomorrow and silently breaks a streak.
//
// This one integer is the whole day key: equality means "same day" and
// subtraction gives elapsed days, so no reverse calendar conversion is needed.
// utcOffsetQuarterHours is signed (UTC+0 = 0, UTC-5 = -20, UTC+14 = 56).
int32_t localDayNumber(int32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute,
                       int32_t utcOffsetQuarterHours);

/**
 * @brief Streak bookkeeping for the decay ladder.
 *
 * Today's task and habit counts each live in their own owning cache
 * (TodoistTaskCache, HabitifyHabitCache), which already resets them daily, so
 * nothing about today's counts is duplicated here. This ledger only remembers
 * the last day that cleared contentPoints and the resulting streak -- state
 * neither of those caches has any reason to know about.
 */
struct DayLedger {
  // Sentinel for "no day has ever qualified".
  static constexpr int32_t NEVER = INT32_MIN;

  int32_t lastQualifyingDay = NEVER;  // last local day that cleared contentPoints
  uint16_t streakDays = 0;
  uint16_t bestStreakDays = 0;
};

// Re-derives whether `today` has now cleared contentPoints of combined
// tasks+habits effort and, the first time it does, extends (or restarts) the
// streak. Idempotent for the rest of the day, and a no-op while today still
// falls short. Returns true when the ledger changed and the caller should
// persist.
bool creditQualifyingDay(DayLedger& ledger, int32_t today, uint16_t tasksCompletedToday, uint16_t habitsCompletedToday,
                         const MoodThresholds& t = {});

// Derives the mood inputs for `today` from a ledger plus the live counts from
// the task and habit caches. When the clock is invalid, day arithmetic is
// skipped and daysSinceLastActive stays 0.
MoodInput moodInputFor(const DayLedger& ledger, int32_t today, bool clockValid, uint16_t tasksCompletedToday,
                       uint16_t habitsCompletedToday);

}  // namespace companion
