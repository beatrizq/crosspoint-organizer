#include "BudgetActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
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

void BudgetActivity::loadCaches() { YNAB_CATEGORIES.loadFromFile(); }

const char* BudgetActivity::screenTitle() const { return tr(STR_ORGANIZER_TAB_BUDGET); }

const char* BudgetActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::PLAN:
      return tr(STR_BUDGET_TAB_PLAN);
    case Tab::ACCOUNT:
      return tr(STR_BUDGET_TAB_ACCOUNT);
  }
  return "";
}

// -- rows -------------------------------------------------------------------

int BudgetActivity::rowCount() const {
  // Account balances come from an endpoint nothing calls yet, so that tab has
  // nothing to list.
  if (static_cast<Tab>(tab()) == Tab::ACCOUNT) return 0;
  return static_cast<int>(YNAB_CATEGORIES.getCategories().size());
}

void BudgetActivity::drawRow(const RowLayout& layout) const {
  const auto& categories = YNAB_CATEGORIES.getCategories();
  if (layout.index < 0 || static_cast<size_t>(layout.index) >= categories.size()) return;
  const YnabCategory& category = categories[static_cast<size_t>(layout.index)];

  // Gap between a category name and its balance, so a long name that had to be
  // truncated does not run into the number. Two spaces of the row's own font, so
  // it tracks the font-size setting.
  const int balanceGap = renderer.getSpaceWidth(layout.titleFont) * 2;
  // The balance is measured first: it is the part being glanced at, so it is
  // drawn whole and the name takes whatever width is left.
  const int balanceWidth = renderer.getTextWidth(layout.titleFont, category.balance.c_str());
  const int nameWidth = std::max(0, layout.width - balanceWidth - balanceGap);

  const auto shownName = renderer.truncatedText(layout.titleFont, category.name.c_str(), nameWidth);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownName.c_str(), layout.ink);
  renderer.drawText(layout.titleFont, layout.x + layout.width - balanceWidth, layout.textY, category.balance.c_str(),
                    layout.ink);
}

void BudgetActivity::formatStatus(char* out, const size_t outSize) const {
  if (static_cast<Tab>(tab()) == Tab::ACCOUNT) {
    // No sync behind this tab yet, so there is no date to stamp it with and
    // nothing to count.
    out[0] = '\0';
    return;
  }
  if (!YNAB_CATEGORIES.hasSynced()) {
    snprintf(out, outSize, "%s", tr(STR_YNAB_NEVER_SYNCED));
    return;
  }
  // The month, not the day: these balances are what YNAB holds for the month as
  // a whole, and a day would imply they moved today.
  char month[16];
  organizer::formatMonthLabel(YNAB_CATEGORIES.getSyncMonth(), month, sizeof(month));
  snprintf(out, outSize, "%s  ·  %s: %zu", month, tr(STR_YNAB_CATEGORIES), YNAB_CATEGORIES.getCategories().size());
}

const char* BudgetActivity::emptyMessage() const {
  if (static_cast<Tab>(tab()) == Tab::ACCOUNT) {
    // Not "no accounts": nothing is fetched for this tab yet, so an empty list
    // here says nothing about the plan's actual accounts.
    return tr(STR_ORGANIZER_NOTHING_YET);
  }
  // Nothing ticked is the usual reason this list is empty, and it is the one the
  // user can act on.
  return YNAB_STORE.getSelectedCategories().empty() ? tr(STR_YNAB_NO_CATEGORIES) : tr(STR_YNAB_NEVER_SYNCED);
}

const char* BudgetActivity::syncingMessage() const { return tr(STR_YNAB_SYNCING); }

// -- sync -------------------------------------------------------------------

void BudgetActivity::startSync() {
  if (static_cast<Tab>(tab()) == Tab::ACCOUNT) {
    // Nothing to fetch yet. Reported rather than ignored, so a hold on this tab
    // does not look like a sync that silently found nothing.
    failSync(tr(STR_ORGANIZER_NOTHING_YET));
    return;
  }
  if (!YNAB_STORE.isConfigured()) {
    failSync(tr(STR_YNAB_NOT_CONFIGURED));
    return;
  }
  if (YNAB_STORE.getSelectedCategories().empty()) {
    failSync(tr(STR_YNAB_NO_CATEGORIES));
    return;
  }
  runSync([this] { performBudgetSync(); });
}

void BudgetActivity::performBudgetSync() {
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
    LOG_ERR("BUDGET", "Budget fetch failed: %s", YnabClient::errorString(error));
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
