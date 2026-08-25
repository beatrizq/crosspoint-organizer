#include "CompanionMood.h"

namespace companion {

Mood evaluate(const MoodInput& in, const MoodThresholds& t) {
  const uint32_t points = static_cast<uint32_t>(in.tasksCompletedToday) + in.habitsCompletedToday;
  if (points >= t.thrivingPoints) return Mood::Thriving;

  // No clock: elapsed days are unknowable, so decay cannot be justified.
  // Happy is the floor rather than punishing a user whose RTC was never set.
  if (!in.clockValid) return Mood::Happy;

  if (points >= t.happyPoints) return Mood::Happy;
  // Zero here means today already qualified earlier, or the clock was just
  // corrected backwards past the last qualifying day -- never a real "did
  // enough yesterday" carry-over, since a day that actually qualified would
  // have made the points check above true. Mood reflects only today's own
  // effort: nothing done today starts the decay immediately, with no grace
  // for what happened on a previous day. That grace only applies when there
  // is history to grace at all -- a companion that has never once qualified
  // has nothing earned to fall back on, so it starts the ladder at Peckish
  // like any other day with zero activity.
  if (in.everQualified && in.daysSinceLastActive == 0) return Mood::Happy;
  if (in.daysSinceLastActive < t.neglectedDays) return Mood::Peckish;
  return Mood::Neglected;
}

int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day) {
  // Shift the era so March starts the year, which makes the leap-day the last
  // day of the cycle and removes every February special case.
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);                   // [0, 399]
  const uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     // [0, 146096]
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

int32_t daysBetween(int32_t fromYear, uint32_t fromMonth, uint32_t fromDay, int32_t toYear, uint32_t toMonth,
                    uint32_t toDay) {
  return daysFromCivil(toYear, toMonth, toDay) - daysFromCivil(fromYear, fromMonth, fromDay);
}

int32_t localDayNumber(int32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute,
                       int32_t utcOffsetQuarterHours) {
  static constexpr int32_t MINUTES_PER_DAY = 1440;
  const int32_t utcMinutes = daysFromCivil(year, month, day) * MINUTES_PER_DAY + static_cast<int32_t>(hour) * 60 +
                             static_cast<int32_t>(minute);
  const int32_t localMinutes = utcMinutes + utcOffsetQuarterHours * 15;
  // Floor division: C++ truncates toward zero, which would put the pre-epoch
  // side of midnight on the wrong day.
  return localMinutes >= 0 ? localMinutes / MINUTES_PER_DAY
                           : -(((-localMinutes) + MINUTES_PER_DAY - 1) / MINUTES_PER_DAY);
}

bool creditQualifyingDay(DayLedger& ledger, const int32_t today, const uint16_t tasksCompletedToday,
                         const uint16_t habitsCompletedToday, const MoodThresholds& t) {
  // Already recorded today: a second or third completion the same day must not
  // extend the streak again.
  if (ledger.lastQualifyingDay == today) return false;

  const uint32_t points = static_cast<uint32_t>(tasksCompletedToday) + habitsCompletedToday;
  if (points < t.happyPoints) return false;

  // This day just cleared the bar for the first time: extend the streak when it
  // directly follows the previous qualifying day, otherwise start a new one.
  const bool consecutive = ledger.lastQualifyingDay != DayLedger::NEVER && today == ledger.lastQualifyingDay + 1;
  ledger.streakDays = consecutive && ledger.streakDays < UINT16_MAX ? ledger.streakDays + 1 : 1;
  if (ledger.streakDays > ledger.bestStreakDays) ledger.bestStreakDays = ledger.streakDays;
  ledger.lastQualifyingDay = today;
  return true;
}

MoodInput moodInputFor(const DayLedger& ledger, const int32_t today, const bool clockValid,
                       const uint16_t tasksCompletedToday, const uint16_t habitsCompletedToday) {
  MoodInput in;
  in.clockValid = clockValid;
  in.tasksCompletedToday = tasksCompletedToday;
  in.habitsCompletedToday = habitsCompletedToday;

  if (!clockValid) return in;

  if (ledger.lastQualifyingDay == DayLedger::NEVER) {
    // Never qualified: a brand new companion has nothing to have neglected
    // yet, but also nothing earned to grace -- everQualified stays false.
    in.daysSinceLastActive = 0;
    return in;
  }

  in.everQualified = true;
  const int32_t elapsed = today - ledger.lastQualifyingDay;
  // A clock corrected backwards yields a negative span; treat it as "today"
  // rather than letting a wild value read as neglect.
  in.daysSinceLastActive = elapsed <= 0 ? 0 : static_cast<uint16_t>(elapsed > UINT16_MAX ? UINT16_MAX : elapsed);
  return in;
}

}  // namespace companion
