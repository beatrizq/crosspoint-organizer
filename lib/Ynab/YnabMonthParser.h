#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

#include "YnabCategory.h"

/** One category, as it appears while the response is being walked. */
struct YnabParsedCategory {
  const char* id;
  const char* name;
  // `balance_formatted`, the balance already rendered in the plan's currency
  // format. "" when the field was absent (API versions before v1.82.0).
  const char* balanceFormatted;
  int64_t balanceMilli;  // `balance` in milliunits; 0 when the field was absent
  bool hidden;
  bool deleted;
};

/**
 * SAX-style extractor for the YNAB "get a plan month" response:
 *
 *   {"data":{"month":{"month":"2026-08-01","income":...,"to_be_budgeted":...,
 *            "categories":[{"id":"...","name":"Rent","hidden":false,
 *                           "balance":50000,"balance_formatted":"$50.00",
 *                           "deleted":false,...}]}}}
 *
 * Only the month itself and the fields the Budget tab draws are kept. Streamed
 * rather than buffered because every category carries its group, note, and a
 * dozen goal fields in both milliunit and formatted forms - a real plan with
 * fifty categories runs to tens of KB, far too much to hold whole beside a live
 * TLS session on a device with ~90KB of free heap.
 *
 * Categories are emitted with hidden/deleted intact rather than filtered here:
 * the sink decides, because the picker and the sync want the same walk but not
 * quite the same rows.
 */
class YnabMonthParser {
 public:
  // Invoked once per category object, as soon as it closes.
  using CategorySink = void (*)(void* ctx, const YnabParsedCategory& category);

  YnabMonthParser(CategorySink sink, void* sinkCtx);

  YnabMonthParser(const YnabMonthParser&) = delete;
  YnabMonthParser& operator=(const YnabMonthParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // The month the amounts belong to, as "YYYY-MM-DD"; "" when absent.
  const char* month() const { return monthIso; }
  // Categories seen in the response, including any the sink chose to drop.
  size_t categoryCount() const { return categoriesSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_CATEGORIES_ARRAY,
    IN_CATEGORY_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    MONTH,
    CATEGORIES,
    CAT_ID,
    CAT_NAME,
    CAT_HIDDEN,
    CAT_DELETED,
    CAT_BALANCE,
    CAT_BALANCE_FORMATTED,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void clearCurrent();
  void commitCategory();

  StreamingJsonParser parser;
  CategorySink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;     // Object/array nesting outside the categories array
  uint8_t catDepth;  // Nesting inside the current category (1 = the entry itself)
  size_t categoriesSeen;

  char monthIso[11];

  // Sized from the display caps, so the walk truncates once and nothing
  // downstream has to cut a string again. Names are user-written, and a plan
  // with an essay for a category name cannot push the parser's footprint
  // around; formatted balances are short ("-1.234.567,89 kr").
  char currentId[YNAB_CATEGORY_ID_MAX_LEN + 1];
  char currentName[YnabCategory::NAME_MAX_LEN + 1];
  char currentBalanceText[YnabCategory::BALANCE_MAX_LEN + 1];
  int64_t currentBalance;
  bool currentHidden;
  bool currentDeleted;
};
