#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "YnabCategory.h"

/**
 * HTTPS client for the YNAB API v1.
 *
 * One endpoint serves both screens:
 *   GET /plans/{plan_id}/months/current   - the month's categories and balances
 *
 * The plan id is what the YNAB app calls the budget id; the API renamed the
 * resource in v1.79.0 and kept /budgets/{budget_id} working as an undocumented
 * alias. The ids are the same either way, so a budget id copied out of the YNAB
 * web app URL works here; the documented path is the one requested.
 *
 * Authentication: Authorization: Bearer <personal access token>
 *
 * The picker and the Budget tab both read this one response - the picker for
 * the category names and ids, the tab for the balances of the categories that
 * were ticked. Fetching the same document twice is deliberate: YNAB allows 200
 * requests per hour per token, and both screens are opened by hand, so a second
 * endpoint would buy nothing but another parser.
 */
class YnabClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NO_BUDGET,  // No plan (budget) id configured
    NETWORK_ERROR,
    AUTH_FAILED,   // 401/403: the token was rejected
    NOT_FOUND,     // 404: no such plan id
    RATE_LIMITED,  // 429: 200 requests per hour per token
    SERVER_ERROR,
    PARSE_ERROR,
    LOW_MEMORY,
  };

  /** A category as offered in the picker. */
  struct CategoryInfo {
    std::string id;
    std::string name;
  };

  // A plan with more categories than this has outgrown a picker on an e-ink
  // reader, and the cap bounds the fetch.
  static constexpr size_t MAX_CATEGORIES_LISTED = 64;

  /** Lists the plan's visible categories, for the selection screen. */
  static Error fetchCategoryList(std::vector<CategoryInfo>& outCategories);

  /**
   * Fetches the balances of the selected categories, for the Budget tab.
   *
   * Categories arrive in the plan's own order and are kept that way. outMonth
   * receives the month the amounts belong to as a packed civil date (the 1st of
   * that month), or civil::NO_DATE when the response carried no month.
   */
  static Error fetchSelectedCategories(std::vector<YnabCategory>& outCategories, uint16_t& outMonth);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
