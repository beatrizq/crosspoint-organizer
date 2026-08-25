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

  // Enabled, Character, Show on Home, Show mood label, Active for.
  static constexpr int MENU_ITEMS = 5;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  // Stamps CompanionState::activatedDay the first time it notices the
  // companion enabled with no activation ever recorded -- whether that is
  // because it was just switched on here, or because it was already on from
  // before this field existed. No-op once activatedDay is already set, and
  // whenever the companion is off or the clock has no reading yet.
  void stampActivationIfNeeded();

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;

  // SettingsActivity dispatches this screen on Confirm going down, so the
  // matching release lands here instead, once this screen is already active.
  // Unswallowed, it reads as a fresh Confirm-release on row 0 (Enabled) and
  // flips it before the user has touched anything. Armed in onEnter() only
  // when Confirm is still physically down at that point.
  bool swallowConfirmRelease = false;
  // The Enabled row's confirmation popup answers on the button going down (see
  // ConfirmationActivity), so its Confirm/Back release lands back here once
  // this screen is active again. Same idea as swallowConfirmRelease above,
  // armed instead from the popup's result handler.
  bool swallowBackRelease = false;
};
