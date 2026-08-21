#pragma once
#include <YnabClient.h>

#include <cstddef>
#include <string>
#include <vector>

#include "OrganizerScreenActivity.h"

/**
 * The Budget screen: the synced YNAB figures, tabbed by what they are.
 *
 * Budget was one tab of the Organizer screen. It is now its own, and the tab bar
 * it inherited splits the two things YNAB holds: Plan leads with the per-category
 * balances, then one tab per open, on-budget account showing that account's most
 * recent transactions, newest first.
 *
 * The tab count follows the accounts cached by Settings -> Organizer -> YNAB ->
 * Accounts, from 1 (Plan alone, before that screen has been visited) to
 * YNAB_MAX_ACCOUNTS + 1. Labels come from the short names recorded there, so the
 * bar reads correctly with the radio off - YNAB's own account names are far too
 * long for a tab.
 *
 * Syncing is per tab, and deliberately so: Plan is one request against a token
 * allowed 200 an hour, and each account is another, so holding Select on a tab
 * fetches that tab rather than everything.
 *
 * Nothing is ever pushed back - the device does not spend money - so every tab is
 * read-only and Select is unlabelled on a row.
 */
class BudgetActivity final : public OrganizerScreenActivity {
 public:
  // Tab 0 is Plan; tab 1..n is the account at index n-1 in the cache.
  static constexpr int PLAN_TAB = 0;

  explicit BudgetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int initialTab = PLAN_TAB)
      : OrganizerScreenActivity("Budget", renderer, mappedInput, initialTab) {}

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return 1 + static_cast<int>(tabLabels.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // A transaction carries its date on a second line; a category balance sits on
  // the row itself.
  bool rowsHaveSubtitle() const override { return tab() != PLAN_TAB; }
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::BUDGET; }
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Budget; }

 private:
  void performPlanSync();
  void performTransactionSync(const std::string& accountId);
  static const char* budgetErrorText(YnabClient::Error error);

  // The account behind the tab being shown, or nullptr on Plan.
  const YnabAccount* currentAccount() const;

  // Tab labels for the accounts, snapshotted when the screen opens. Held as
  // strings rather than derived on demand because tabLabel() hands out a
  // const char* that has to outlive the call, and because the tab count must not
  // change under the selection model mid-screen.
  void rebuildTabs();
  std::vector<std::string> tabLabels;
};
