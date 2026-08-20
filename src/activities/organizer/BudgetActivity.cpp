#include "BudgetActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <YnabAccountCache.h>
#include <YnabAccountLabel.h>
#include <YnabCategoryCache.h>
#include <YnabStore.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "OrganizerLabels.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

void BudgetActivity::loadCaches() {
  YNAB_CATEGORIES.loadFromFile();
  YNAB_ACCOUNTS.loadFromFile();
  rebuildTabs();
}

void BudgetActivity::rebuildTabs() {
  tabLabels.clear();
  const auto& accounts = YNAB_ACCOUNTS.getAccounts();
  tabLabels.reserve(accounts.size());
  for (const auto& account : accounts) tabLabels.push_back(ynabAccountLabel(account));
}

const char* BudgetActivity::screenTitle() const { return tr(STR_ORGANIZER_TAB_BUDGET); }

const char* BudgetActivity::tabLabel(const int index) const {
  if (index == PLAN_TAB) return tr(STR_BUDGET_TAB_PLAN);
  const int account = index - 1;
  if (account < 0 || static_cast<size_t>(account) >= tabLabels.size()) return "";
  return tabLabels[static_cast<size_t>(account)].c_str();
}

const YnabAccount* BudgetActivity::currentAccount() const {
  if (tab() == PLAN_TAB) return nullptr;
  return YNAB_ACCOUNTS.accountAt(static_cast<size_t>(tab() - 1));
}

// -- rows -------------------------------------------------------------------

int BudgetActivity::rowCount() const {
  if (tab() == PLAN_TAB) return static_cast<int>(YNAB_CATEGORIES.getCategories().size());
  const YnabAccount* account = currentAccount();
  return account == nullptr ? 0 : static_cast<int>(account->transactions.size());
}

void BudgetActivity::drawRow(const RowLayout& layout) const {
  // Both kinds of row put an amount hard right and let the label take what is
  // left: the amount is the part being glanced at, so it is drawn whole. Two
  // spaces of the row's own font separate them, so the gap tracks the font-size
  // setting and a truncated label cannot run into the number.
  const int amountGap = renderer.getSpaceWidth(layout.titleFont) * 2;

  const char* labelText = nullptr;
  const char* amountText = nullptr;
  char when[16];
  when[0] = '\0';
  // Inflow is the money not yet assigned anywhere, so it is the total the rows
  // under it draw down. The cache sorts it to the front when it is ticked; bold
  // is what marks it as the summary rather than another category.
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;

  if (tab() == PLAN_TAB) {
    const auto& categories = YNAB_CATEGORIES.getCategories();
    if (layout.index < 0 || static_cast<size_t>(layout.index) >= categories.size()) return;
    const YnabCategory& category = categories[static_cast<size_t>(layout.index)];
    labelText = category.name.c_str();
    amountText = category.balance.c_str();
    if (isYnabInflowCategory(category.name)) style = EpdFontFamily::BOLD;
  } else {
    const YnabAccount* account = currentAccount();
    if (account == nullptr) return;
    if (layout.index < 0 || static_cast<size_t>(layout.index) >= account->transactions.size()) return;
    const YnabTransaction& transaction = account->transactions[static_cast<size_t>(layout.index)];
    labelText = transaction.payee.c_str();
    amountText = transaction.amount.c_str();
    organizer::formatDayLabel(transaction.date, when, sizeof(when));
  }

  const int amountWidth = renderer.getTextWidth(layout.titleFont, amountText, style);
  const int labelWidth = std::max(0, layout.width - amountWidth - amountGap);

  const auto shownLabel = renderer.truncatedText(layout.titleFont, labelText, labelWidth, style);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownLabel.c_str(), layout.ink, style);
  renderer.drawText(layout.titleFont, layout.x + layout.width - amountWidth, layout.textY, amountText, layout.ink,
                    style);

  if (when[0] != '\0') {
    const int whenY = layout.textY + renderer.getLineHeight(layout.titleFont);
    renderer.drawText(layout.subtitleFont, layout.x, whenY, when, layout.ink);
    // Greyed, so the date stays subordinate to the payee and the amount.
    dimText(layout.x, whenY, layout.subtitleFont, when, layout.ink);
  }
}

void BudgetActivity::formatStatus(char* out, const size_t outSize) const {
  if (tab() == PLAN_TAB) {
    if (!YNAB_CATEGORIES.hasSynced()) {
      snprintf(out, outSize, "%s", tr(STR_YNAB_NEVER_SYNCED));
      return;
    }
    // The month, not the day: these balances are what YNAB holds for the month as
    // a whole, and a day would imply they moved today.
    char month[16];
    organizer::formatMonthLabel(YNAB_CATEGORIES.getSyncMonth(), month, sizeof(month));
    snprintf(out, outSize, "%s  ·  %s: %zu", month, tr(STR_YNAB_CATEGORIES), YNAB_CATEGORIES.getCategories().size());
    return;
  }

  const YnabAccount* account = currentAccount();
  if (account == nullptr) {
    out[0] = '\0';
    return;
  }
  // The account's own balance leads: it is the number the tab exists to answer,
  // and the transactions below explain it. The date is the day they were fetched.
  if (account->transactionsSyncDate == civil::NO_DATE) {
    snprintf(out, outSize, "%s  ·  %s", account->balance.c_str(), tr(STR_YNAB_NEVER_SYNCED));
    return;
  }
  char date[16];
  organizer::formatDayLabel(account->transactionsSyncDate, date, sizeof(date));
  snprintf(out, outSize, "%s  ·  %s", account->balance.c_str(), date);
}

const char* BudgetActivity::emptyMessage() const {
  if (tab() != PLAN_TAB) {
    const YnabAccount* account = currentAccount();
    if (account == nullptr) return tr(STR_ORGANIZER_NOTHING_YET);
    return account->transactionsSyncDate == civil::NO_DATE ? tr(STR_YNAB_NEVER_SYNCED) : tr(STR_YNAB_NO_TRANSACTIONS);
  }
  // Nothing ticked is the usual reason this list is empty, and it is the one the
  // user can act on.
  return YNAB_STORE.getSelectedCategories().empty() ? tr(STR_YNAB_NO_CATEGORIES) : tr(STR_YNAB_NEVER_SYNCED);
}

const char* BudgetActivity::syncingMessage() const {
  return tab() == PLAN_TAB ? tr(STR_YNAB_SYNCING) : tr(STR_YNAB_SYNCING_TRANSACTIONS);
}

// -- sync -------------------------------------------------------------------

void BudgetActivity::startSync() {
  if (!YNAB_STORE.isConfigured()) {
    failSync(tr(STR_YNAB_NOT_CONFIGURED));
    return;
  }

  if (tab() != PLAN_TAB) {
    const YnabAccount* account = currentAccount();
    if (account == nullptr) {
      // The tab exists but the cache does not have it any more, which means the
      // accounts screen has not been back since the plan changed.
      failSync(tr(STR_YNAB_NO_ACCOUNTS));
      return;
    }
    // Copied, not held: the sync repaints and reloads the cache under it, so a
    // pointer into the account vector would not survive the fetch.
    const std::string accountId = account->id;
    runSync([this, accountId] { performTransactionSync(accountId); });
    return;
  }

  if (YNAB_STORE.getSelectedCategories().empty()) {
    failSync(tr(STR_YNAB_NO_CATEGORIES));
    return;
  }
  runSync([this] { performPlanSync(); });
}

void BudgetActivity::performPlanSync() {
  // One request, and it carries its own month: the balances are month-scoped
  // and YNAB says which month it answered for, so nothing here needs a clock.
  // That matters on boards with no RTC, and it keeps this sync to a single
  // call against a token allowed 200 requests an hour.
  std::vector<YnabCategory> fetched;
  uint16_t month = civil::NO_DATE;
  resetTaskWatchdogIfSubscribed();
  const YnabClient::Error error = YnabClient::fetchSelectedCategories(fetched, month);
  resetTaskWatchdogIfSubscribed();
  if (error != YnabClient::OK) {
    LOG_ERR("BUDGET", "Plan fetch failed: %s", YnabClient::errorString(error));
  }

  // Drop the radio before touching the SD card and repainting; the full
  // teardown happens on the silent reboot in onExit().
  tearDownRadio();

  if (error == YnabClient::OK) {
    RenderLock lock(*this);
    YNAB_CATEGORIES.setCategories(std::move(fetched), month);
  }
  finishSync(error == YnabClient::OK ? nullptr : budgetErrorText(error));
  YNAB_CATEGORIES.saveToFile();
}

void BudgetActivity::performTransactionSync(const std::string& accountId) {
  // One request for this account alone. The account list is not refetched here:
  // which accounts exist is a setup question the accounts screen owns, and asking
  // again on every tab sync would spend a second request an hour on something
  // that changes once a year.
  std::vector<YnabTransaction> fetched;
  uint16_t date = civil::NO_DATE;
  resetTaskWatchdogIfSubscribed();
  const YnabClient::Error error = YnabClient::fetchTransactions(accountId, fetched, date);
  resetTaskWatchdogIfSubscribed();
  if (error != YnabClient::OK) {
    LOG_ERR("BUDGET", "Transaction fetch failed: %s", YnabClient::errorString(error));
  }

  tearDownRadio();

  if (error == YnabClient::OK) {
    RenderLock lock(*this);
    // The cache sorts newest-first and trims to YNAB_MAX_TRANSACTIONS.
    YNAB_ACCOUNTS.setTransactions(accountId, std::move(fetched), date);
  }
  finishSync(error == YnabClient::OK ? nullptr : budgetErrorText(error));
  YNAB_ACCOUNTS.saveToFile();
}

const char* BudgetActivity::budgetErrorText(const YnabClient::Error error) {
  switch (error) {
    case YnabClient::NO_TOKEN:
    case YnabClient::NO_BUDGET:
      return tr(STR_YNAB_NOT_CONFIGURED);
    case YnabClient::AUTH_FAILED:
      return tr(STR_YNAB_INVALID_TOKEN);
    case YnabClient::NOT_FOUND:
      return tr(STR_YNAB_BUDGET_NOT_FOUND);
    case YnabClient::RATE_LIMITED:
      return tr(STR_YNAB_RATE_LIMITED);
    case YnabClient::SERVER_ERROR:
      return tr(STR_YNAB_SERVER_ERROR);
    case YnabClient::PARSE_ERROR:
      return tr(STR_YNAB_BAD_RESPONSE);
    case YnabClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}
