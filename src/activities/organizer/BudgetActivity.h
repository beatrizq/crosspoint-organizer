#pragma once
#include <YnabClient.h>

#include <cstddef>

#include "OrganizerScreenActivity.h"

/**
 * The Budget screen: the synced YNAB balances, split across its own tab bar.
 *
 * Budget was one tab of the Organizer screen. It is now its own screen, and the
 * tab bar it inherited splits the two things YNAB holds balances for: Plan, the
 * per-category amounts the existing sync fetches, and Account, the on-budget
 * account balances.
 *
 * Account needs a second YNAB endpoint that nothing calls yet, so it is an empty
 * tab for now.
 *
 * Nothing is ever pushed back - the device does not spend money - so both tabs
 * are read-only and Select is unlabelled on a row.
 */
class BudgetActivity final : public OrganizerScreenActivity {
 public:
  enum class Tab : uint8_t { PLAN = 0, ACCOUNT = 1 };
  static constexpr int TAB_COUNT = 2;

  explicit BudgetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Tab initialTab = Tab::PLAN)
      : OrganizerScreenActivity("Budget", renderer, mappedInput, static_cast<int>(initialTab)) {}

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return TAB_COUNT; }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::BUDGET; }

 private:
  void performBudgetSync();
  static const char* budgetErrorText(YnabClient::Error error);
};
