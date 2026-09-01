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

  void loadEntries();

 public:
  explicit LogsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Logs", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
