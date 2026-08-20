#pragma once
#include <YnabAccount.h>
#include <YnabClient.h>

#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Names the accounts the Budget screen gives a tab.
 *
 * The list is fetched live, like the category picker's: accounts are opened and
 * closed as a plan evolves, this screen is only opened deliberately, and a stale
 * list would hide one the user had just added. Only open, on-budget accounts come
 * back - YNAB's closed and tracking accounts are not things you check daily.
 *
 * Selecting a row opens the keyboard on that account's label. The label is what
 * the tab bar draws, because YNAB has no short-name field: an account is called
 * whatever the owner typed in the app, and "Barclays Current Account" is three
 * times wider than a tab. An account with no label falls back to the first word
 * of its name, which for a bank account is usually the bank.
 *
 * The fetched list is cached on the way out, so the Budget screen knows which
 * tabs to draw without needing the radio.
 */
class YnabAccountsActivity final : public Activity {
 public:
  explicit YnabAccountsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("YnabAccounts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LOADING,  // Fetching the account list
    LIST,
    FAILED,
  };

  void fetchAccounts();
  void editSelectedLabel();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::LOADING;
  const char* statusMessage = nullptr;
  std::vector<YnabAccount> accounts;
  bool dirty = false;  // A label changed and needs persisting on exit
};
