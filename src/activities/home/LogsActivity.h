#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * "Logs": today's completed tasks and habits in one read-only list, reached
 * from QuickPickActivity's Left button. Reads straight from TodoistTaskCache/
 * HabitifyHabitCache's own "completed today" data (see their own comments) --
 * both are offline-first caches already, so opening this needs no Wi-Fi and
 * triggers no sync, the same way QuickPickActivity itself works.
 *
 * Styled like Tasks/Habits (title/subtitle font following
 * SETTINGS.organizerFontSize, dithered separators/selection) without
 * inheriting OrganizerScreenActivity: that base class also brings tabs and an
 * unconditional Wi-Fi-teardown reboot-on-exit this screen has no use for.
 * BleNotificationsActivity established this same standalone pattern first.
 */
class LogsActivity final : public Activity {
  struct Entry {
    std::string title;  // Task content, or habit name
    bool isHabit;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Entry> entries;
  size_t selectorIndex = 0;

  // Guards against a stale release firing right after ConfirmationActivity
  // closes -- same pattern and reasoning as CompanionSettingsActivity's own
  // pair of these (some button handling in that popup answers on the press,
  // not the release, so the matching release can still be pending when
  // control returns here).
  bool swallowBackRelease = false;
  bool swallowConfirmRelease = false;

  void loadEntries();
  // Confirms, then zeroes both caches' today-scoped completion data via
  // TodoistTaskCache::clearCompletedNow()/HabitifyHabitCache::
  // clearCompletedNow() -- the same reset the automatic day-rollover already
  // does on its own, just user-triggered on demand from the Confirm button.
  void offerClear();

 public:
  explicit LogsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Logs", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
