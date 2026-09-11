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
 * it inherited splits the three things YNAB holds: Plan leads with the per-category
 * balances, then Today pools every account's transactions dated today into one
 * list, then one tab per open, on-budget account showing that account's most
 * recent transactions, newest first.
 *
 * The tab count follows the accounts cached by Settings -> Organizer -> YNAB ->
 * Accounts, from 2 (Plan and Today alone, before that screen has been visited) to
 * YNAB_MAX_ACCOUNTS + 2. Labels come from the short names recorded there, so the
 * bar reads correctly with the radio off - YNAB's own account names are far too
 * long for a tab.
 *
 * Syncing is per tab, and deliberately so: Plan is one request against a token
 * allowed 200 an hour, and each account is another, so holding Select on a tab
 * fetches that tab rather than everything. Today has nothing of its own to
 * sync - it only pools whatever each account's own tab already fetched - so
 * holding Select there says so instead of fetching anything.
 *
 * Nothing is ever pushed back - the device does not spend money - so every tab is
 * read-only and Select is unlabelled on a row.
 */
class BudgetActivity final : public OrganizerScreenActivity {
 public:
  // Tab 0 is Plan, tab 1 is Today, tab 2..n+1 is the account at index n in the
  // cache.
  static constexpr int PLAN_TAB = 0;
  static constexpr int TODAY_TAB = 1;

  explicit BudgetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int initialTab = PLAN_TAB)
      : OrganizerScreenActivity("Budget", renderer, mappedInput, initialTab) {}

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return 2 + static_cast<int>(tabLabels.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // A transaction carries a second line (the account name on Today, the date
  // on an account tab); a category balance sits on the row itself on Plan.
  bool rowsHaveSubtitle() const override { return tab() != PLAN_TAB; }
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::BUDGET; }

 private:
  void performPlanSync();
  void performTransactionSync(const std::string& accountId);
  static const char* budgetErrorText(YnabClient::Error error);

  // The account behind the tab being shown, or nullptr on Plan or Today.
  const YnabAccount* currentAccount() const;

  // One transaction Today pools in: which account it came from and its index
  // into that account's own transactions vector. Indices, not a copy of the
  // transaction itself, since the underlying vectors do not move between a
  // rebuild and the next one (only setTransactions()/setAccounts() replace
  // them, both of which trigger a rebuild first).
  struct TodayEntry {
    uint8_t accountIndex;
    uint8_t transactionIndex;
  };

  // Tab labels for the accounts, snapshotted when the screen opens. Held as
  // strings rather than derived on demand because tabLabel() hands out a
  // const char* that has to outlive the call, and because the tab count must not
  // change under the selection model mid-screen. Rebuilds todayEntries too (see
  // its own comment), so every call site that used to just need fresh labels
  // now gets a fresh Today pool along with them at no extra cost worth a
  // separate call - at most YNAB_MAX_ACCOUNTS accounts, this is cheap.
  void rebuildTabs();
  std::vector<std::string> tabLabels;

  // Every transaction dated on its own account's last sync day, pooled across
  // every account -- accounts sync independently (see
  // YnabAccountCache::getTodayTransactionCount(), which this mirrors), so
  // there is no single fetch this list is "the result of"; it is a live pool
  // over whatever each account's own tab last fetched. Newest account order,
  // then newest-first within each (transactions are already sorted that way
  // per account).
  std::vector<TodayEntry> todayEntries;
};
