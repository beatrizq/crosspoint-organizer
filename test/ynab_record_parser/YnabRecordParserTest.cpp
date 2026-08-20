#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "YnabRecordParser.h"

namespace {

// The two field tables the client uses, duplicated here on purpose: the test is
// what pins the slot numbering, so a table edited on one side without the other
// fails rather than silently reading the wrong slot.
enum AccountSlot { ACC_ID = 0, ACC_NAME = 1, ACC_BALANCE_TEXT = 2 };
enum AccountBool { ACC_ON_BUDGET = 0, ACC_CLOSED = 1, ACC_DELETED = 2 };

const YnabFieldSpec ACCOUNT_FIELDS[] = {
    {"id", YnabFieldKind::String, ACC_ID},
    {"name", YnabFieldKind::String, ACC_NAME},
    {"balance_formatted", YnabFieldKind::String, ACC_BALANCE_TEXT},
    {"balance", YnabFieldKind::Number, 0},
    {"on_budget", YnabFieldKind::Bool, ACC_ON_BUDGET},
    {"closed", YnabFieldKind::Bool, ACC_CLOSED},
    {"deleted", YnabFieldKind::Bool, ACC_DELETED},
};

enum TxSlot { TX_DATE = 0, TX_PAYEE = 1, TX_MEMO = 2, TX_AMOUNT_TEXT = 3 };

const YnabFieldSpec TRANSACTION_FIELDS[] = {
    {"date", YnabFieldKind::String, TX_DATE}, {"payee_name", YnabFieldKind::String, TX_PAYEE},
    {"memo", YnabFieldKind::String, TX_MEMO}, {"amount_formatted", YnabFieldKind::String, TX_AMOUNT_TEXT},
    {"amount", YnabFieldKind::Number, 0},     {"deleted", YnabFieldKind::Bool, 0},
};

struct Record {
  std::string strings[YnabParsedRecord::MAX_STRINGS];
  int64_t number;
  bool bools[YnabParsedRecord::MAX_BOOLS];
};

std::vector<Record> g_records;

void sink(void*, const YnabParsedRecord& record) {
  Record out{};
  for (size_t i = 0; i < YnabParsedRecord::MAX_STRINGS; i++) out.strings[i] = record.strings[i];
  out.number = record.numbers[0];
  for (size_t i = 0; i < YnabParsedRecord::MAX_BOOLS; i++) out.bools[i] = record.bools[i];
  g_records.push_back(std::move(out));
}

// Feeds the document in irregular chunks, which is how it arrives off a TLS
// socket. A parser that only works on whole buffers passes the naive test and
// fails on the device.
void feedInChunks(YnabRecordParser& parser, const std::string& json) {
  size_t offset = 0;
  while (offset < json.size()) {
    size_t n = (offset % 7) + 1;
    if (offset + n > json.size()) n = json.size() - offset;
    parser.feed(json.data() + offset, n);
    offset += n;
  }
}

class YnabRecordParserTest : public ::testing::Test {
 protected:
  void SetUp() override { g_records.clear(); }
};

// An accounts response with every shape the screen has to survive: the wrapping
// data object, an on-budget account, a closed one, an off-budget tracking
// account, one with no formatted balance (an API version before the formatted
// fields), and nested objects whose keys collide with ones we keep.
constexpr char ACCOUNTS_JSON[] = R"({"data":{"accounts":[
  {"id":"acc-1","name":"Barclays Current Account","type":"checking","on_budget":true,"closed":false,
   "balance":1234560,"balance_formatted":"£1,234.56","deleted":false,
   "note":null,"transfer_payee_id":"pay-1"},
  {"id":"acc-2","name":"Old Savings","type":"savings","on_budget":true,"closed":true,
   "balance":0,"balance_formatted":"£0.00","deleted":false},
  {"id":"acc-3","name":"Mortgage","type":"mortgage","on_budget":false,"closed":false,
   "balance":-98765430,"balance_formatted":"-£98,765.43","deleted":false},
  {"id":"acc-4","name":"No Formatted","type":"checking","on_budget":true,"closed":false,
   "balance":-4560,"deleted":false,
   "debt_interest_rates":{"id":"nested-should-not-win","name":"nested"}}
]}})";

TEST_F(YnabRecordParserTest, ParsesAccountsAndFlags) {
  YnabRecordParser parser("accounts", ACCOUNT_FIELDS, sizeof(ACCOUNT_FIELDS) / sizeof(ACCOUNT_FIELDS[0]), sink,
                          nullptr);
  feedInChunks(parser, ACCOUNTS_JSON);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_records.size(), 4u);
  EXPECT_EQ(parser.recordCount(), 4u);

  EXPECT_EQ(g_records[0].strings[ACC_ID], "acc-1");
  EXPECT_EQ(g_records[0].strings[ACC_NAME], "Barclays Current Account");
  EXPECT_EQ(g_records[0].strings[ACC_BALANCE_TEXT], "£1,234.56");
  EXPECT_EQ(g_records[0].number, 1234560);
  EXPECT_TRUE(g_records[0].bools[ACC_ON_BUDGET]);
  EXPECT_FALSE(g_records[0].bools[ACC_CLOSED]);

  EXPECT_TRUE(g_records[1].bools[ACC_CLOSED]);
  EXPECT_FALSE(g_records[2].bools[ACC_ON_BUDGET]);
  EXPECT_EQ(g_records[2].number, -98765430);

  // No balance_formatted in the response leaves the slot empty, so the client
  // knows to fall back to formatting the milliunits itself.
  EXPECT_EQ(g_records[3].strings[ACC_BALANCE_TEXT], "");
  EXPECT_EQ(g_records[3].number, -4560);
  // A nested object's "id"/"name" must not overwrite the record's own.
  EXPECT_EQ(g_records[3].strings[ACC_ID], "acc-4");
  EXPECT_EQ(g_records[3].strings[ACC_NAME], "No Formatted");
}

// A transactions response with the shapes that actually turn up: a null payee, a
// null memo, a subtransactions array nested inside a record, and a deleted row.
constexpr char TRANSACTIONS_JSON[] = R"({"data":{"transactions":[
  {"id":"t-1","date":"2026-08-17","amount":-24300,"amount_formatted":"-£24.30",
   "payee_name":"Caffè Nero","memo":"flat white","cleared":"cleared","deleted":false},
  {"id":"t-2","date":"2026-08-16","amount":-1200,"payee_name":null,"memo":"cash",
   "deleted":false,"subtransactions":[{"date":"1999-01-01","memo":"nested"}]},
  {"id":"t-3","date":"2026-08-15","amount":250000,"amount_formatted":"£250.00",
   "payee_name":"Salary","memo":null,"deleted":true}
]}})";

TEST_F(YnabRecordParserTest, ParsesTransactionsWithNullsAndNesting) {
  YnabRecordParser parser("transactions", TRANSACTION_FIELDS,
                          sizeof(TRANSACTION_FIELDS) / sizeof(TRANSACTION_FIELDS[0]), sink, nullptr);
  feedInChunks(parser, TRANSACTIONS_JSON);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_records.size(), 3u);

  EXPECT_EQ(g_records[0].strings[TX_DATE], "2026-08-17");
  EXPECT_EQ(g_records[0].strings[TX_PAYEE], "Caffè Nero");
  EXPECT_EQ(g_records[0].strings[TX_MEMO], "flat white");
  EXPECT_EQ(g_records[0].strings[TX_AMOUNT_TEXT], "-£24.30");
  EXPECT_EQ(g_records[0].number, -24300);

  // A null payee leaves the slot empty so the caller can fall back to the memo.
  EXPECT_EQ(g_records[1].strings[TX_PAYEE], "");
  EXPECT_EQ(g_records[1].strings[TX_MEMO], "cash");
  EXPECT_EQ(g_records[1].strings[TX_AMOUNT_TEXT], "");
  // The nested subtransaction must not overwrite the record's own date or memo.
  EXPECT_EQ(g_records[1].strings[TX_DATE], "2026-08-16");

  EXPECT_TRUE(g_records[2].bools[0]);
  EXPECT_EQ(g_records[2].strings[TX_MEMO], "");
}

TEST_F(YnabRecordParserTest, IgnoresOtherArraysAtTopLevel) {
  // The named array is what matters; a sibling array of objects must not feed
  // records, or a response that grows a new list would double the rows.
  constexpr char json[] = R"({"data":{"payees":[{"id":"p-1","name":"Not an account"}],)"
                          R"("accounts":[{"id":"acc-1","name":"Real","on_budget":true,"closed":false}]}})";
  YnabRecordParser parser("accounts", ACCOUNT_FIELDS, sizeof(ACCOUNT_FIELDS) / sizeof(ACCOUNT_FIELDS[0]), sink,
                          nullptr);
  feedInChunks(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_records.size(), 1u);
  EXPECT_EQ(g_records[0].strings[ACC_NAME], "Real");
}

TEST_F(YnabRecordParserTest, TruncatesLongStringsOnCodepointBoundary) {
  // A name longer than the slot, ending mid-multibyte-character. The cut must
  // land on a boundary, so the row draws a clipped word rather than a stray glyph.
  std::string longName;
  while (longName.size() < YnabRecordParser::STRING_CAPACITY + 8) longName += "é";
  const std::string json = R"({"data":{"accounts":[{"id":"a","name":")" + longName + R"("}]}})";

  YnabRecordParser parser("accounts", ACCOUNT_FIELDS, sizeof(ACCOUNT_FIELDS) / sizeof(ACCOUNT_FIELDS[0]), sink,
                          nullptr);
  feedInChunks(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_records.size(), 1u);
  const std::string& name = g_records[0].strings[ACC_NAME];
  EXPECT_LT(name.size(), YnabRecordParser::STRING_CAPACITY);
  // Every byte pair is a complete two-byte sequence, so the length is even.
  EXPECT_EQ(name.size() % 2, 0u);
}

TEST_F(YnabRecordParserTest, EmptyArrayYieldsNoRecords) {
  YnabRecordParser parser("accounts", ACCOUNT_FIELDS, sizeof(ACCOUNT_FIELDS) / sizeof(ACCOUNT_FIELDS[0]), sink,
                          nullptr);
  feedInChunks(parser, R"({"data":{"accounts":[]}})");
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(g_records.size(), 0u);
}

}  // namespace
