#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the YNAB integration.
 *
 * Holds the one-time setup: the personal access token generated in the owner's
 * own YNAB account, the budget (plan) id to read, which categories feed the
 * Budget screen's Plan tab, and the short labels for its account tabs. Syncing
 * itself happens on that screen, not here.
 *
 * There is no pairing flow to run: YNAB issues long-lived personal access
 * tokens, so "connecting" is just entering the token and the budget id, after
 * which the category picker can reach the API.
 */
class YnabSettingsActivity final : public Activity {
 public:
  explicit YnabSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("YnabSettings", renderer, mappedInput) {}

  // Access Token, Budget ID, Categories, Accounts, Clear Token, and the sync
  // hint row.
  static constexpr int MENU_ITEMS = 7;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    MENU,    // The settings list
    FAILED,  // statusMessage holds the reason
  };

  void handleSelection();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::MENU;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state

  // SettingsActivity dispatches this screen on Confirm going down, so the
  // matching release lands here instead, once this screen is already active.
  // Unswallowed, it reads as a fresh Confirm-release on row 0 (Nickname) and
  // opens the keyboard before the user has touched anything. Armed in
  // onEnter() only when Confirm is still physically down at that point.
  bool swallowConfirmRelease = false;
};
