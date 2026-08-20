#include "YnabClient.h"

#include <Arduino.h>
#include <CivilTime.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

#include <cstdio>
#include <utility>

#include "YnabMonthParser.h"
#include "YnabRecordParser.h"
#include "YnabStore.h"

int YnabClient::lastHttpCode = 0;

namespace {

constexpr char API_BASE[] = "https://api.ynab.com/v1";

// Same TLS heap gate as TodoistClient and GCalClient.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("YNC", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap, MIN_FREE_FOR_TLS,
            maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

// Percent-encodes everything outside the unreserved set. Plan ids are UUIDs and
// go in a path segment, but the value is typed by hand, so a stray character
// must not be able to reshape the URL.
std::string urlEncode(const std::string& value) {
  // Not named HEX: Arduino's Print.h defines that as a macro (`#define HEX 16`).
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() + 8);
  for (const unsigned char c : value) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(HEX_DIGITS[c >> 4]);
      out.push_back(HEX_DIGITS[c & 0x0F]);
    }
  }
  return out;
}

YnabClient::Error errorForStatus(const int httpCode) {
  if (httpCode <= 0) return YnabClient::NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return YnabClient::OK;
  if (httpCode == 401 || httpCode == 403) return YnabClient::AUTH_FAILED;
  if (httpCode == 404) return YnabClient::NOT_FOUND;
  if (httpCode == 429) return YnabClient::RATE_LIMITED;
  return YnabClient::SERVER_ERROR;
}

/**
 * Renders milliunits as a plain decimal, e.g. -1234560 -> "-1234.56".
 *
 * Only used when the response carried no `balance_formatted` (API versions
 * before v1.82.0): no currency symbol and no separators, because neither can be
 * guessed from the amount alone. The maths is done in 64 bits - milliunits
 * overflow int32 at ~2.1 million units, which is an ordinary balance in
 * currencies like the rupiah - but printed in 32-bit pieces, because the
 * newlib-nano formatter linked into the firmware has no 64-bit conversions.
 */
void formatMilliunits(const int64_t milli, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  const bool negative = milli < 0;
  // Negated as unsigned so INT64_MIN cannot overflow the negation itself.
  const uint64_t magnitude = negative ? ~static_cast<uint64_t>(milli) + 1u : static_cast<uint64_t>(milli);
  // Milliunits carry three decimals; balances are shown to two, rounded.
  const uint64_t hundredths = (magnitude + 5u) / 10u;
  const uint64_t units = hundredths / 100u;
  snprintf(out, outSize, "%s%lu.%02lu", negative ? "-" : "",
           static_cast<unsigned long>(units > 0xFFFFFFFFu ? 0xFFFFFFFFu : units),
           static_cast<unsigned long>(hundredths % 100u));
}

void assignBalanceText(const YnabParsedCategory& category, std::string& out) {
  if (category.balanceFormatted[0] != '\0') {
    // Already capped at BALANCE_MAX_LEN by the parser, on a codepoint boundary.
    out = category.balanceFormatted;
    return;
  }
  char fallback[24];
  formatMilliunits(category.balanceMilli, fallback, sizeof(fallback));
  out = fallback;
}

// -- picker sink ------------------------------------------------------------

struct ListCollector {
  std::vector<YnabClient::CategoryInfo>* out;
};

void collectForList(void* ctx, const YnabParsedCategory& category) {
  auto* collector = static_cast<ListCollector*>(ctx);
  if (collector->out->size() >= YnabClient::MAX_CATEGORIES_LISTED) return;
  // Hidden categories are archived in YNAB and deleted ones are tombstones;
  // neither belongs in a list of things to tick.
  if (category.hidden || category.deleted) return;

  YnabClient::CategoryInfo info;
  info.id = category.id;
  // A category with no name is not expected, but a blank picker row would be
  // untickable in practice.
  info.name = category.name[0] != '\0' ? category.name : category.id;
  collector->out->push_back(std::move(info));
}

// -- Budget tab sink --------------------------------------------------------

struct BalanceCollector {
  std::vector<YnabCategory>* out;
};

void collectSelected(void* ctx, const YnabParsedCategory& category) {
  auto* collector = static_cast<BalanceCollector*>(ctx);
  if (collector->out->size() >= YNAB_MAX_CATEGORIES) return;
  if (category.deleted) return;
  // Matched against the stored selection here rather than after the walk, so a
  // plan's worth of categories never exists in RAM at once.
  if (!YNAB_STORE.isCategorySelected(category.id)) return;

  YnabCategory row;
  row.name = category.name[0] != '\0' ? category.name : category.id;
  assignBalanceText(category, row.balance);
  collector->out->push_back(std::move(row));
}

/**
 * Runs the month request, feeding the body through the parser as it arrives.
 *
 * outMonth is left untouched unless the response carried a month.
 */
YnabClient::Error requestCurrentMonth(const YnabMonthParser::CategorySink sink, void* sinkCtx, uint16_t* outMonth) {
  YnabClient::lastHttpCode = 0;
  if (!YNAB_STORE.hasToken()) {
    LOG_DBG("YNC", "No access token configured");
    return YnabClient::NO_TOKEN;
  }
  if (!YNAB_STORE.hasBudgetId()) {
    LOG_DBG("YNC", "No budget id configured");
    return YnabClient::NO_BUDGET;
  }
  if (insufficientHeap()) return YnabClient::LOW_MEMORY;

  // On the heap, not the stack: the parser embeds the streaming tokenizer's
  // 512-byte buffer, well past what a task stack here should carry.
  auto parser = makeUniqueNoThrow<YnabMonthParser>(sink, sinkCtx);
  if (!parser) {
    LOG_ERR("YNC", "OOM: YnabMonthParser");
    return YnabClient::LOW_MEMORY;
  }

  std::string url = API_BASE;
  url += "/plans/";
  url += urlEncode(YNAB_STORE.getBudgetId());
  url += "/months/current";

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("YNC", "Bad month URL");
    return YnabClient::NETWORK_ERROR;
  }
  http.addHeader("Authorization", "Bearer " + YNAB_STORE.getAccessToken());
  http.addHeader("Accept", "application/json");

  // Streamed into the parser as it arrives: a plan's categories run to tens of
  // KB of JSON, which would not fit alongside a live TLS session.
  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  http.end();
  YnabClient::lastHttpCode = httpCode;
  LOG_DBG("YNC", "months/current: %d (%zu categories, month %s)", httpCode, parser->categoryCount(),
          parser->month()[0] != '\0' ? parser->month() : "?");

  const YnabClient::Error status = errorForStatus(httpCode);
  if (status != YnabClient::OK) return status;
  if (parser->hasError()) {
    LOG_ERR("YNC", "Malformed month JSON");
    return YnabClient::PARSE_ERROR;
  }
  if (outMonth != nullptr) {
    const uint16_t month = civil::dateFromIso(parser->month());
    if (month != civil::NO_DATE) *outMonth = month;
  }
  return YnabClient::OK;
}

// -- accounts and transactions ----------------------------------------------

// Slot numbering for the two record tables. Mirrored by
// test/ynab_record_parser, which is what stops a table and its reader drifting.
enum AccountSlot : uint8_t { ACC_ID = 0, ACC_NAME = 1, ACC_BALANCE_TEXT = 2 };
enum AccountBool : uint8_t { ACC_ON_BUDGET = 0, ACC_CLOSED = 1, ACC_DELETED = 2 };

constexpr YnabFieldSpec ACCOUNT_FIELDS[] = {
    {"id", YnabFieldKind::String, ACC_ID},
    {"name", YnabFieldKind::String, ACC_NAME},
    {"balance_formatted", YnabFieldKind::String, ACC_BALANCE_TEXT},
    {"balance", YnabFieldKind::Number, 0},
    {"on_budget", YnabFieldKind::Bool, ACC_ON_BUDGET},
    {"closed", YnabFieldKind::Bool, ACC_CLOSED},
    {"deleted", YnabFieldKind::Bool, ACC_DELETED},
};

enum TxSlot : uint8_t { TX_DATE = 0, TX_PAYEE = 1, TX_MEMO = 2, TX_AMOUNT_TEXT = 3 };

constexpr YnabFieldSpec TRANSACTION_FIELDS[] = {
    {"date", YnabFieldKind::String, TX_DATE}, {"payee_name", YnabFieldKind::String, TX_PAYEE},
    {"memo", YnabFieldKind::String, TX_MEMO}, {"amount_formatted", YnabFieldKind::String, TX_AMOUNT_TEXT},
    {"amount", YnabFieldKind::Number, 0},     {"deleted", YnabFieldKind::Bool, 0},
};

// Reuses the category path's fallback: prefer what the API rendered, and only
// format milliunits when the response predates the formatted fields.
void assignAmountText(const char* formatted, const int64_t milli, std::string& out) {
  if (formatted[0] != '\0') {
    out = formatted;
    return;
  }
  char fallback[24];
  formatMilliunits(milli, fallback, sizeof(fallback));
  out = fallback;
}

struct AccountCollector {
  std::vector<YnabAccount>* out;
};

void collectAccount(void* ctx, const YnabParsedRecord& record) {
  auto* collector = static_cast<AccountCollector*>(ctx);
  if (collector->out->size() >= YNAB_MAX_ACCOUNTS) return;
  if (record.strings[ACC_ID][0] == '\0') return;
  // Closed accounts are archived and deleted ones are tombstones; off-budget
  // accounts are YNAB's tracking accounts, which are not what a tab bar of
  // accounts-you-check is for.
  if (record.bools[ACC_CLOSED] || record.bools[ACC_DELETED] || !record.bools[ACC_ON_BUDGET]) return;

  YnabAccount account;
  account.id = record.strings[ACC_ID];
  // An unnamed account would draw a blank row in the label editor, so the id
  // stands in - it is at least selectable.
  account.name = record.strings[ACC_NAME][0] != '\0' ? record.strings[ACC_NAME] : record.strings[ACC_ID];
  assignAmountText(record.strings[ACC_BALANCE_TEXT], record.numbers[0], account.balance);
  collector->out->push_back(std::move(account));
}

struct TransactionCollector {
  std::vector<YnabTransaction>* out;
};

void collectTransaction(void* ctx, const YnabParsedRecord& record) {
  auto* collector = static_cast<TransactionCollector*>(ctx);
  if (record.bools[0]) return;  // deleted: a tombstone, not a row

  YnabTransaction transaction;
  // A transaction with no payee is normally a manual entry, where the memo is
  // what the user wrote to identify it. Neither being set leaves the row named
  // by its amount alone, which is still a row worth showing.
  transaction.payee = record.strings[TX_PAYEE][0] != '\0' ? record.strings[TX_PAYEE] : record.strings[TX_MEMO];
  assignAmountText(record.strings[TX_AMOUNT_TEXT], record.numbers[0], transaction.amount);
  transaction.date = civil::dateFromIso(record.strings[TX_DATE]);

  // Held to twice what is kept, dropping from the front when full. The API
  // returns oldest-first, so the newest rows arrive last: cutting at the cap
  // would keep exactly the wrong end. Twice YNAB_MAX_TRANSACTIONS leaves the
  // cache a tail worth sorting without a month of rows ever being in RAM. The
  // eviction is after the row is built, so a deleted entry cannot displace a
  // real one.
  auto& out = *collector->out;
  if (out.size() >= YNAB_MAX_TRANSACTIONS * 2) out.erase(out.begin());
  out.push_back(std::move(transaction));
}

/**
 * Runs a plan-scoped GET, feeding the body through a record parser as it arrives.
 *
 * `path` is appended to /plans/{plan_id}, already encoded.
 */
YnabClient::Error requestRecords(const std::string& path, const char* arrayKey, const YnabFieldSpec* fields,
                                 const size_t fieldCount, YnabRecordParser::RecordSink sink, void* sinkCtx,
                                 uint16_t* outDate) {
  YnabClient::lastHttpCode = 0;
  if (!YNAB_STORE.hasToken()) {
    LOG_DBG("YNC", "No access token configured");
    return YnabClient::NO_TOKEN;
  }
  if (!YNAB_STORE.hasBudgetId()) {
    LOG_DBG("YNC", "No budget id configured");
    return YnabClient::NO_BUDGET;
  }
  if (insufficientHeap()) return YnabClient::LOW_MEMORY;

  // On the heap, not the stack: the parser embeds the streaming tokenizer's
  // 512-byte buffer plus its string slots, well past what a task stack here
  // should carry.
  auto parser = makeUniqueNoThrow<YnabRecordParser>(arrayKey, fields, fieldCount, sink, sinkCtx);
  if (!parser) {
    LOG_ERR("YNC", "OOM: YnabRecordParser");
    return YnabClient::LOW_MEMORY;
  }

  std::string url = API_BASE;
  url += "/plans/";
  url += urlEncode(YNAB_STORE.getBudgetId());
  url += path;

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("YNC", "Bad %s URL", arrayKey);
    return YnabClient::NETWORK_ERROR;
  }
  http.addHeader("Authorization", "Bearer " + YNAB_STORE.getAccessToken());
  http.addHeader("Accept", "application/json");

  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });

  // Read before end(): the parsed headers belong to this connection.
  const std::string dateHeader = outDate != nullptr ? http.getHeader("date") : std::string();
  http.end();
  YnabClient::lastHttpCode = httpCode;
  LOG_DBG("YNC", "%s: %d (%zu records)", arrayKey, httpCode, parser->recordCount());

  const YnabClient::Error status = errorForStatus(httpCode);
  if (status != YnabClient::OK) return status;
  if (parser->hasError()) {
    LOG_ERR("YNC", "Malformed %s JSON", arrayKey);
    return YnabClient::PARSE_ERROR;
  }
  if (outDate != nullptr && !dateHeader.empty()) {
    const uint16_t date = civil::dateFromHttpHeader(dateHeader.c_str());
    if (date != civil::NO_DATE) *outDate = date;
  }
  return YnabClient::OK;
}

}  // namespace

YnabClient::Error YnabClient::fetchCategoryList(std::vector<CategoryInfo>& outCategories) {
  outCategories.clear();
  ListCollector collector{&outCategories};
  const Error error = requestCurrentMonth(collectForList, &collector, nullptr);
  if (error != OK) outCategories.clear();
  return error;
}

YnabClient::Error YnabClient::fetchSelectedCategories(std::vector<YnabCategory>& outCategories, uint16_t& outMonth) {
  outCategories.clear();
  outCategories.reserve(YNAB_STORE.getSelectedCategories().size());
  BalanceCollector collector{&outCategories};
  const Error error = requestCurrentMonth(collectSelected, &collector, &outMonth);
  if (error != OK) outCategories.clear();
  return error;
}

YnabClient::Error YnabClient::fetchAccounts(std::vector<YnabAccount>& outAccounts) {
  outAccounts.clear();
  outAccounts.reserve(YNAB_MAX_ACCOUNTS);
  AccountCollector collector{&outAccounts};
  const Error error =
      requestRecords("/accounts", "accounts", ACCOUNT_FIELDS, sizeof(ACCOUNT_FIELDS) / sizeof(ACCOUNT_FIELDS[0]),
                     collectAccount, &collector, nullptr);
  if (error != OK) outAccounts.clear();
  return error;
}

YnabClient::Error YnabClient::fetchTransactions(const std::string& accountId,
                                                std::vector<YnabTransaction>& outTransactions, uint16_t& outDate) {
  outTransactions.clear();
  if (accountId.empty()) return NOT_FOUND;
  outTransactions.reserve(YNAB_MAX_TRANSACTIONS * 2);
  TransactionCollector collector{&outTransactions};
  const std::string path = "/accounts/" + urlEncode(accountId) + "/transactions";
  const Error error = requestRecords(path, "transactions", TRANSACTION_FIELDS,
                                     sizeof(TRANSACTION_FIELDS) / sizeof(TRANSACTION_FIELDS[0]), collectTransaction,
                                     &collector, &outDate);
  if (error != OK) outTransactions.clear();
  return error;
}

const char* YnabClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "ok";
    case NO_TOKEN:
      return "no access token";
    case NO_BUDGET:
      return "no budget id";
    case NETWORK_ERROR:
      return "network error";
    case AUTH_FAILED:
      return "access token rejected";
    case NOT_FOUND:
      return "budget not found";
    case RATE_LIMITED:
      return "rate limited";
    case SERVER_ERROR:
      return "server error";
    case PARSE_ERROR:
      return "bad response";
    case LOW_MEMORY:
      return "low memory";
  }
  return "unknown";
}
