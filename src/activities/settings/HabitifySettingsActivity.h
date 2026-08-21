#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the Habitify integration.
 *
 * Holds the one-time setup: the API key generated in Habitify's own app, and
 * whether the Habits screen hides habits that have met their goal. Syncing itself
 * happens on that screen, not here.
 *
 * There is no pairing flow to run: Habitify issues a long-lived key from Settings
 * -> API in its own app, so "connecting" is just entering it. Note that it keeps
 * one key active per account - generating a new one there invalidates whatever is
 * on this device.
 */
class HabitifySettingsActivity final : public Activity {
 public:
  explicit HabitifySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HabitifySettings", renderer, mappedInput) {}

  // API Key, Hide Completed, Clear Key, and the sync hint row.
  static constexpr int MENU_ITEMS = 4;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
};
