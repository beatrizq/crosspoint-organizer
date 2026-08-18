#pragma once
#include <cstddef>
#include <string>

/**
 * One row of the Organizer's Budget tab: a YNAB category and its available
 * balance.
 *
 * The balance is kept as display text rather than as a number because YNAB
 * returns `balance_formatted` already rendered in the plan's own currency
 * format - symbol, placement, group and decimal separators, decimal digits.
 * Reproducing that here would mean a second request for the plan settings plus
 * a currency formatter, to rebuild a string the API already sent. Milliunits
 * are only formatted locally as a fallback (see YnabClient), for plans on an
 * API version predating the formatted fields.
 *
 * Ordering is the API's own, which is the category order shown in YNAB itself.
 */
struct YnabCategory {
  std::string name;
  std::string balance;  // Display text, e.g. "$1,234.56"

  static constexpr size_t NAME_MAX_LEN = 48;
  static constexpr size_t BALANCE_MAX_LEN = 24;
};

// A category id is a 36-character UUID. Ids are cut to this length everywhere -
// the parser's buffer and the stored selection - so an id ticked in the picker
// still matches the same category on the next sync.
static constexpr size_t YNAB_CATEGORY_ID_MAX_LEN = 40;

// Categories the Budget tab can show at once. A glanceable balance list stops
// being glanceable well before this, and the cap bounds both the fetch and the
// cache file.
static constexpr size_t YNAB_MAX_CATEGORIES = 16;
