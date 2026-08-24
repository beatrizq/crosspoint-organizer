#include <gtest/gtest.h>

#include <set>

#include "Companion/CompanionMood.h"

using companion::Mood;
using companion::MoodInput;
using companion::MoodThresholds;

namespace {

MoodInput withClock(uint16_t tasksToday, uint16_t habitsToday, uint16_t daysSince) {
  MoodInput in;
  in.tasksCompletedToday = tasksToday;
  in.habitsCompletedToday = habitsToday;
  in.daysSinceLastActive = daysSince;
  in.clockValid = true;
  return in;
}

}  // namespace

// ---------------------------------------------------------------- mood ladder

TEST(CompanionMood, EnoughTasksAloneIsThriving) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints, 0, 0)), Mood::Thriving);
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints + 5, 0, 0)), Mood::Thriving);
}

TEST(CompanionMood, EnoughHabitsAloneIsThriving) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(0, t.thrivingPoints, 0)), Mood::Thriving);
}

TEST(CompanionMood, TasksAndHabitsMixToReachThriving) {
  const MoodThresholds t;
  ASSERT_GE(t.thrivingPoints, 2);
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints - 1, 1, 0)), Mood::Thriving);
}

TEST(CompanionMood, JustUnderThrivingIsStillContent) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints - 1, 0, 0)), Mood::Content);
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints, 0, 0)), Mood::Thriving);
}

TEST(CompanionMood, AnyCompletionTodayIsContent) {
  EXPECT_EQ(companion::evaluate(withClock(1, 0, 0)), Mood::Content);
  EXPECT_EQ(companion::evaluate(withClock(0, 1, 0)), Mood::Content);
}

TEST(CompanionMood, QualifiedYesterdayKeepsContentGrace) {
  // Nothing yet today, but yesterday counted: no penalty until a day is skipped.
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 1)), Mood::Content);
}

TEST(CompanionMood, OneSkippedDayIsPeckish) { EXPECT_EQ(companion::evaluate(withClock(0, 0, 2)), Mood::Peckish); }

TEST(CompanionMood, ThreeQuietDaysIsNeglected) {
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 3)), Mood::Neglected);
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 90)), Mood::Neglected);
}

TEST(CompanionMood, NeglectIsRecoverableTheSameDay) {
  // The chosen design has no death state: enough activity from the floor goes
  // straight back to the top.
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 400)), Mood::Neglected);
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints, 0, 400)), Mood::Thriving);
}

TEST(CompanionMood, ThresholdsAreConfigurable) {
  MoodThresholds relaxed;
  relaxed.thrivingPoints = 1;
  relaxed.neglectedDays = 7;
  EXPECT_EQ(companion::evaluate(withClock(1, 0, 0), relaxed), Mood::Thriving);
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 5), relaxed), Mood::Peckish);
  EXPECT_EQ(companion::evaluate(withClock(0, 0, 7), relaxed), Mood::Neglected);
}

// -------------------------------------------------------- clockless fallback

TEST(CompanionMood, WithoutClockNeverFallsBelowContent) {
  MoodInput in;
  in.clockValid = false;
  in.tasksCompletedToday = 0;
  in.habitsCompletedToday = 0;
  in.daysSinceLastActive = 999;  // garbage without a clock; must be ignored
  EXPECT_EQ(companion::evaluate(in), Mood::Content);
}

TEST(CompanionMood, WithoutClockThrivingStillReachable) {
  // Today's task/habit counts are live reads independent of the RTC, so
  // Thriving must stay reachable even when the clock is invalid.
  const MoodThresholds t;
  MoodInput in;
  in.clockValid = false;
  in.tasksCompletedToday = t.thrivingPoints;
  EXPECT_EQ(companion::evaluate(in), Mood::Thriving);
}

// ------------------------------------------------------------ calendar maths

TEST(CompanionCalendar, EpochIsDayZero) { EXPECT_EQ(companion::daysFromCivil(1970, 1, 1), 0); }

TEST(CompanionCalendar, KnownDates) {
  EXPECT_EQ(companion::daysFromCivil(1969, 12, 31), -1);
  EXPECT_EQ(companion::daysFromCivil(1970, 1, 2), 1);
  EXPECT_EQ(companion::daysFromCivil(2000, 3, 1), 11017);
  EXPECT_EQ(companion::daysFromCivil(2026, 8, 18), 20683);
}

TEST(CompanionCalendar, ConsecutiveDaysAlwaysDifferByOne) {
  // Walks several years day by day, which catches month-length and leap-year
  // errors that spot checks miss.
  static constexpr uint8_t kLengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int32_t previous = companion::daysFromCivil(2023, 1, 1);
  for (int32_t y = 2023; y <= 2029; ++y) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    for (uint32_t m = 1; m <= 12; ++m) {
      const uint32_t last = kLengths[m - 1] + (leap && m == 2 ? 1u : 0u);
      for (uint32_t d = 1; d <= last; ++d) {
        if (y == 2023 && m == 1 && d == 1) continue;
        const int32_t current = companion::daysFromCivil(y, m, d);
        EXPECT_EQ(current - previous, 1) << y << "-" << m << "-" << d;
        previous = current;
      }
    }
  }
}

TEST(CompanionCalendar, LeapDayIsHandled) {
  EXPECT_EQ(companion::daysBetween(2024, 2, 28, 2024, 3, 1), 2);  // 2024 is a leap year
  EXPECT_EQ(companion::daysBetween(2023, 2, 28, 2023, 3, 1), 1);
  EXPECT_EQ(companion::daysBetween(2100, 2, 28, 2100, 3, 1), 1);  // century, not a leap year
}

TEST(CompanionCalendar, SpansMonthAndYearBoundaries) {
  EXPECT_EQ(companion::daysBetween(2025, 12, 31, 2026, 1, 1), 1);
  EXPECT_EQ(companion::daysBetween(2026, 1, 31, 2026, 2, 1), 1);
  EXPECT_EQ(companion::daysBetween(2026, 1, 1, 2027, 1, 1), 365);
}

TEST(CompanionCalendar, BackwardsClockYieldsNegative) {
  // Caller treats a negative span as "the clock moved backwards", not as neglect.
  EXPECT_LT(companion::daysBetween(2026, 8, 18, 2026, 8, 17), 0);
}

// ------------------------------------------------------------ local day key

TEST(CompanionLocalDay, MatchesUtcAtZeroOffset) {
  EXPECT_EQ(companion::localDayNumber(1970, 1, 1, 0, 0, 0), 0);
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 12, 0, 0), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, EveningWestOfGreenwichStaysOnTheSameLocalDay) {
  // 02:30 UTC on the 19th is 21:30 on the 18th in UTC-5. Activity then must
  // still count as the 18th, or the streak breaks at the wrong moment.
  const int32_t offsetMinus5 = -20;  // quarter-hours
  EXPECT_EQ(companion::localDayNumber(2026, 8, 19, 2, 30, offsetMinus5), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, MorningEastOfGreenwichRollsForward) {
  // 22:00 UTC on the 18th is 07:00 on the 19th in UTC+9.
  const int32_t offsetPlus9 = 36;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 22, 0, offsetPlus9), companion::daysFromCivil(2026, 8, 19));
}

TEST(CompanionLocalDay, HandlesExtremeOffsets) {
  const int32_t offsetPlus14 = 56;
  const int32_t offsetMinus12 = -48;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 12, 0, offsetPlus14), companion::daysFromCivil(2026, 8, 19));
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 6, 0, offsetMinus12), companion::daysFromCivil(2026, 8, 17));
}

TEST(CompanionLocalDay, QuarterHourOffsetsWork) {
  // Nepal is UTC+05:45.
  const int32_t offsetPlus545 = 23;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 18, 20, offsetPlus545), companion::daysFromCivil(2026, 8, 19));
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 18, 10, offsetPlus545), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, ConsecutiveLocalDaysDifferByOne) {
  const int32_t offset = -20;
  const int32_t d1 = companion::localDayNumber(2026, 12, 31, 20, 0, offset);
  const int32_t d2 = companion::localDayNumber(2027, 1, 1, 20, 0, offset);
  EXPECT_EQ(d2 - d1, 1);
}

// ------------------------------------------------------------- day ledger

using companion::DayLedger;

namespace {
constexpr int32_t kDay = 20000;  // arbitrary local day number
}  // namespace

TEST(CompanionLedger, FirstQualifyingDayStartsAStreak) {
  DayLedger led;
  EXPECT_TRUE(companion::creditQualifyingDay(led, kDay, 1, 0));
  EXPECT_EQ(led.streakDays, 1);
  EXPECT_EQ(led.bestStreakDays, 1);
  EXPECT_EQ(led.lastQualifyingDay, kDay);
}

TEST(CompanionLedger, SecondCreditSameDayDoesNotDoubleCount) {
  DayLedger led;
  EXPECT_TRUE(companion::creditQualifyingDay(led, kDay, 1, 0));
  // A second task completed later the same day must not extend the streak
  // again.
  EXPECT_FALSE(companion::creditQualifyingDay(led, kDay, 2, 0));
  EXPECT_EQ(led.streakDays, 1);
}

TEST(CompanionLedger, ConsecutiveDaysExtendTheStreak) {
  DayLedger led;
  for (int32_t d = 0; d < 5; ++d) companion::creditQualifyingDay(led, kDay + d, 1, 0);
  EXPECT_EQ(led.streakDays, 5);
  EXPECT_EQ(led.bestStreakDays, 5);
}

TEST(CompanionLedger, GapResetsStreakButKeepsBest) {
  DayLedger led;
  for (int32_t d = 0; d < 4; ++d) companion::creditQualifyingDay(led, kDay + d, 1, 0);
  ASSERT_EQ(led.streakDays, 4);
  companion::creditQualifyingDay(led, kDay + 10, 1, 0);  // week-long gap
  EXPECT_EQ(led.streakDays, 1);
  EXPECT_EQ(led.bestStreakDays, 4);
}

TEST(CompanionLedger, ZeroActivityDoesNotQualifyTheDay) {
  DayLedger led;
  EXPECT_FALSE(companion::creditQualifyingDay(led, kDay, 0, 0));
  EXPECT_EQ(led.streakDays, 0);
  EXPECT_EQ(led.lastQualifyingDay, DayLedger::NEVER);
}

TEST(CompanionLedger, HabitsAloneQualifyJustLikeTasks) {
  DayLedger led;
  EXPECT_TRUE(companion::creditQualifyingDay(led, kDay, 0, 1));
  EXPECT_EQ(led.streakDays, 1);
}

TEST(CompanionLedger, MoodInputReportsTodaysCounts) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 2, 1);
  const auto in = companion::moodInputFor(led, kDay, true, 2, 1);
  EXPECT_EQ(in.tasksCompletedToday, 2);
  EXPECT_EQ(in.habitsCompletedToday, 1);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Thriving);
}

TEST(CompanionLedger, MoodInputReflectsLiveCountsOnANewDay) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3, 0);
  // A new day: the caller passes 0 (yesterday's completions do not carry),
  // and the ledger reports one day since the last qualifying day.
  const auto in = companion::moodInputFor(led, kDay + 1, true, 0, 0);
  EXPECT_EQ(in.tasksCompletedToday, 0);
  EXPECT_EQ(in.daysSinceLastActive, 1);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Content);
}

TEST(CompanionLedger, MoodDecaysAcrossQuietDays) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3, 0);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 2, true, 0, 0)), companion::Mood::Peckish);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 3, true, 0, 0)), companion::Mood::Neglected);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 60, true, 0, 0)), companion::Mood::Neglected);
}

TEST(CompanionLedger, FreshCompanionIsContentNotNeglected) {
  // Nothing has ever qualified, so there is nothing to have neglected.
  const DayLedger led;
  const auto in = companion::moodInputFor(led, kDay, true, 0, 0);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Content);
}

TEST(CompanionLedger, BackwardsClockDoesNotReadAsNeglect) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3, 0);
  const auto in = companion::moodInputFor(led, kDay - 5, true, 0, 0);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_NE(companion::evaluate(in), companion::Mood::Neglected);
}

TEST(CompanionLedger, ClocklessModeUsesLiveCountsOnly) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3, 0);
  const auto idle = companion::moodInputFor(led, kDay + 99, false, 0, 0);
  EXPECT_FALSE(idle.clockValid);
  EXPECT_EQ(companion::evaluate(idle), companion::Mood::Content);

  const MoodThresholds t;
  const auto active = companion::moodInputFor(led, kDay + 99, false, t.thrivingPoints, 0);
  EXPECT_EQ(active.tasksCompletedToday, t.thrivingPoints);
  EXPECT_EQ(companion::evaluate(active), companion::Mood::Thriving);
}

// ------------------------------------------------------------ reachability
// Every tier must be reachable by a plausible sequence of real behaviour, and
// each must be exited again. A mood nobody can reach is dead art.

TEST(CompanionReachability, EveryMoodOccursOverALivedTimeline) {
  DayLedger led;
  std::set<Mood> seen;
  const auto observe = [&](int32_t day, uint16_t tasksToday, uint16_t habitsToday) {
    seen.insert(companion::evaluate(companion::moodInputFor(led, day, true, tasksToday, habitsToday)));
  };

  int32_t day = kDay;
  // Two solid days of activity.
  companion::creditQualifyingDay(led, day, 3, 0);
  observe(day, 3, 0);      // enough today
  observe(day + 1, 0, 0);  // yesterday's grace
  companion::creditQualifyingDay(led, day + 1, 1, 0);
  observe(day + 1, 1, 0);

  // Then it goes quiet.
  observe(day + 2, 0, 0);  // one day after the last qualifying day
  observe(day + 3, 0, 0);  // a full day skipped
  observe(day + 4, 0, 0);  // and another
  observe(day + 9, 0, 0);  // long gone

  EXPECT_EQ(seen.count(Mood::Thriving), 1u) << "Thriving unreachable";
  EXPECT_EQ(seen.count(Mood::Content), 1u) << "Content unreachable";
  EXPECT_EQ(seen.count(Mood::Peckish), 1u) << "Peckish unreachable";
  EXPECT_EQ(seen.count(Mood::Neglected), 1u) << "Neglected unreachable";
  EXPECT_EQ(seen.size(), 4u);
}

TEST(CompanionReachability, PeckishIsNotSkippedOnTheWayDown) {
  // The narrowest tier: it exists for exactly one day, between the grace day
  // and the neglected floor. An off-by-one anywhere would step straight past it.
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3, 0);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 0, true, 3, 0)), Mood::Thriving);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 1, true, 0, 0)), Mood::Content);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 2, true, 0, 0)), Mood::Peckish);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 3, true, 0, 0)), Mood::Neglected);
}

TEST(CompanionReachability, EveryTierIsExitableBackToTheTop) {
  // Recovery must work from the floor, with no penalty box. Starts at one quiet
  // day: with zero the companion is already Thriving on day 0's credit, so
  // there is nothing to recover from.
  const MoodThresholds t;
  for (int32_t quietDays : {1, 2, 3, 50, 5000}) {
    DayLedger led;
    companion::creditQualifyingDay(led, kDay, t.thrivingPoints, 0);
    const int32_t today = kDay + quietDays;
    ASSERT_NE(companion::evaluate(companion::moodInputFor(led, today, true, 0, 0)), Mood::Thriving) << quietDays;

    companion::creditQualifyingDay(led, today, t.thrivingPoints, 0);
    EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, today, true, t.thrivingPoints, 0)), Mood::Thriving)
        << "could not recover after " << quietDays << " quiet days";
  }
}

TEST(CompanionReachability, ThresholdBoundariesAreExact) {
  const MoodThresholds t;
  ASSERT_GT(t.thrivingPoints, t.contentPoints) << "Thriving must sit above Content or a tier is unreachable";

  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints - 1, 0, 0)), Mood::Content) << "one point short of Thriving";
  EXPECT_EQ(companion::evaluate(withClock(t.thrivingPoints, 0, 0)), Mood::Thriving) << "exactly Thriving";

  DayLedger low;
  EXPECT_FALSE(companion::creditQualifyingDay(low, kDay, t.contentPoints - 1, 0))
      << "below contentPoints must not qualify the day";
  EXPECT_EQ(low.lastQualifyingDay, DayLedger::NEVER);
  EXPECT_TRUE(companion::creditQualifyingDay(low, kDay, t.contentPoints, 0)) << "exactly contentPoints must qualify";
  EXPECT_EQ(low.lastQualifyingDay, kDay);
}

TEST(CompanionReachability, EveryMoodHasArtAndIsIndexable) {
  // The enum is used to index the generated sprite and quote tables, so the
  // values must stay contiguous from zero with no gaps.
  EXPECT_EQ(static_cast<int>(Mood::Thriving), 0);
  EXPECT_EQ(static_cast<int>(Mood::Content), 1);
  EXPECT_EQ(static_cast<int>(Mood::Peckish), 2);
  EXPECT_EQ(static_cast<int>(Mood::Neglected), 3);
}
