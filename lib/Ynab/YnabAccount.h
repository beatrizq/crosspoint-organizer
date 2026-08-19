#pragma once
#include <CivilTime.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * One transaction row on a Budget account tab.
 *
 * The amount is kept as display text for the same reason a category balance is:
 * YNAB returns it already rendered in the plan's own currency format, and
 * rebuilding that locally would mean a second request for the plan settings plus
 * a currency formatter. Milliunits are only formatted locally as a fallback, for
 * plans on an API version predating the formatted fields.
 *
 * The date is packed to 2 bytes rather than kept as the "YYYY-MM-DD" the API
 * sends, so a full list costs the two strings plus two bytes per row.
 */
struct YnabTransaction {
  std::string payee;   // payee_name, falling back to the memo
  std::string amount;  // Display text, e.g. "-$24.30"
  uint16_t date = civil::NO_DATE;

  static constexpr size_t PAYEE_MAX_LEN = 48;
  static constexpr size_t AMOUNT_MAX_LEN = 24;
};

/**
 * One account the Budget screen gives a tab, with the transactions behind it.
 *
 * `name` is YNAB's own account name, which is whatever the owner typed in the
 * app - often too long for a tab ("Barclays Current Account"). The tab label
 * comes from the nickname in YnabStore instead, keyed by this id; the name is
 * kept because the nickname editor has to show which account it is editing.
 *
 * Transactions live on the account rather than in a separate map so one cache
 * file holds a self-contained list per tab, and a per-account sync only has to
 * replace this account's own vector.
 */
struct YnabAccount {
  std::string id;
  std::string name;
  std::string balance;  // Display text, as on the Plan tab
  std::vector<YnabTransaction> transactions;
  // When the transactions were last fetched. NO_DATE means never: the account
  // came from an account-list fetch and its tab has not been synced yet.
  uint16_t transactionsSyncDate = civil::NO_DATE;

  static constexpr size_t NAME_MAX_LEN = 48;
  static constexpr size_t BALANCE_MAX_LEN = 24;
};

// A reader showing more account tabs than this has lost the plot, and the cap
// bounds the fetch, the cache file and the tab bar alike.
static constexpr size_t YNAB_MAX_ACCOUNTS = 6;

// Transactions kept per account. A glanceable list stops being glanceable well
// before this, and the cap bounds the fetch and the cache file.
static constexpr size_t YNAB_MAX_TRANSACTIONS = 25;

// An account id is a 36-character UUID, cut to this length everywhere so an id
// stored against a nickname still matches the same account on the next fetch.
static constexpr size_t YNAB_ACCOUNT_ID_MAX_LEN = 40;

// Nicknames are tab labels, so short by design.
static constexpr size_t YNAB_ACCOUNT_NICKNAME_MAX_LEN = 12;
