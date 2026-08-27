#pragma once
#include <CompanionMood.h>

#include <cstdint>

#include "CompanionSprites.generated.h"

/**
 * @brief Runtime glue between the task/habit caches, the RTC, and CompanionState.
 *
 * Reading the RTC costs an I2C transaction, so the day is resolved on demand
 * (screen entry, a completion) and cached; currentMood() only reads the cache
 * plus the in-RAM task/habit lists, so it is safe to call from render paths.
 */
class CompanionTracker {
 public:
  static CompanionTracker& getInstance() {
    static CompanionTracker instance;
    return instance;
  }

  CompanionTracker(const CompanionTracker&) = delete;
  CompanionTracker& operator=(const CompanionTracker&) = delete;

  // True when the user has switched the companion on. Every hook is a no-op
  // otherwise, so the stock organizer paths are untouched when disabled.
  static bool isEnabled();

  // Active character, clamped so a settings value from a newer firmware (or a
  // hand-edited settings.json) cannot index past the sprite table.
  static companion::CompanionId activeId();

  // Resolves the calendar day (one I2C read) so currentMood() is cheap from
  // the render path. Call from a lifecycle hook such as onEnter, never from a
  // render path. Home calls this every time it is entered.
  void refreshForDisplay();

  // Call right after a task completes or habit progress changes, locally or
  // via sync. Re-resolves the day, then credits today's combined tasks+habits
  // total into the ledger -- the qualifying-day marker the first time it
  // clears the bar this day, and the best-day-points record on every call
  // that beats it -- and persists only when something actually changed.
  void recordActivity();

  // Cheap: uses the cached day plus live reads of today's task/habit counts
  // from their own caches. No I2C, no SD.
  companion::Mood currentMood() const;

  // Combined tasks+habits points credited today, the same figure evaluate()
  // sums against MoodThresholds. A progress hint built on it can never
  // disagree with the pose on screen.
  uint16_t pointsToday() const;

  // False when the board has no RTC or it was never set. Day-based decay and
  // streaks are paused in that case; the UI can explain why.
  bool hasValidClock() const { return clockValid; }

  // Resolves "today" as a local day number straight from the RTC, independent
  // of whether the companion is enabled or any cached state. Used to stamp
  // CompanionState::activatedDay and to show how long the companion has been
  // active even while it is currently disabled. Returns false (outDay
  // untouched) when the clock has no usable reading yet.
  static bool resolveLocalDay(int32_t& outDay);

 private:
  CompanionTracker() = default;

  // Reads the RTC and recomputes the cached local day plus local
  // minute-of-day. Does I2C.
  void refreshDay();

  // Single RTC read behind both resolveLocalDay() and refreshDay(), so the day
  // and the intraday minute they derive always come from the same reading
  // rather than two I2C transactions that could straddle a midnight rollover.
  static bool resolveLocalDayAndMinute(int32_t& outDay, uint16_t& outMinuteOfDay);

  // Habits marked complete right now, read live from HABITIFY_HABITS -- it
  // already resets daily on its own, so nothing about it needs persisting
  // here.
  static uint16_t liveHabitsCompletedToday();

  // Single source for the mood inputs, so the pose and any figure shown beside
  // it are always derived from the same numbers.
  companion::MoodInput buildMoodInput() const;

  // Cheap: cached minute-of-day plus the settings fields, no I2C. Gated on
  // clockValid the same way the Milestone check is -- a stale minuteOfDay
  // from before the clock was last valid must not accidentally match.
  bool isWithinSleepWindow() const;

  int32_t localDay = 0;
  uint16_t localMinuteOfDay = 0;
  bool clockValid = false;
};

#define COMPANION CompanionTracker::getInstance()
