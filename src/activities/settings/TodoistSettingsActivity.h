#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the Todoist integration: enter or clear the API token,
 * plus the hint for where syncing happens (the Today screen, not here).
 */
class TodoistSettingsActivity final : public Activity {
 public:
  explicit TodoistSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TodoistSettings", renderer, mappedInput) {}

  // API Token, Sleep Screen, Clear Token, and the non-interactive sync hint row.
  static constexpr int MENU_ITEMS = 5;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();
  // Puts the wallpaper and the sleep screen mode back as they were before the
};
