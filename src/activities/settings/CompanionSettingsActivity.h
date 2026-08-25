#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the organizing companion: enable/disable, character
 * picker, and Home screen placement. Reached from the Organizer tab like the
 * other integrations (Todoist, Habitify, etc.), even though there is nothing
 * to sync here -- everything it controls is local, not an account to connect.
 */
class CompanionSettingsActivity final : public Activity {
 public:
  explicit CompanionSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CompanionSettings", renderer, mappedInput) {}

  // Enabled, Character, Show on Home, Show mood label.
  static constexpr int MENU_ITEMS = 4;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;
};
