#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "YnabMonthParser.h"

namespace {

struct ParsedCategory {
  std::string id;
  std::string name;
  std::string balanceFormatted;
  int64_t balanceMilli;
  bool hidden;
  bool deleted;
};

std::vector<ParsedCategory> g_categories;
void categorySink(void*, const YnabParsedCategory& category) {
  g_categories.push_back({category.id, category.name, category.balanceFormatted, category.balanceMilli, category.hidden,
                          category.deleted});
}

// Feeds the document in irregular chunks, which is how it arrives off a TLS
// socket. A parser that only works on whole buffers passes the naive test and
// fails on the device.
void feedInChunks(YnabMonthParser& parser, const std::string& json) {
  size_t offset = 0;
  while (offset < json.size()) {
    size_t n = (offset % 7) + 1;
    if (offset + n > json.size()) n = json.size() - offset;
    parser.feed(json.data() + offset, n);
    offset += n;
  }
}

// A response exercising every shape the screen has to survive: the wrapping
// data/month objects, a negative balance, a category with no formatted balance
// (an API version before v1.82.0), a hidden one, a deleted one, and goal fields
// whose names start like the ones being matched.
const char* const MONTH_JSON = R"({"data":{"month":{
  "month":"2026-08-01","note":null,"income":4200000,"budgeted":3000000,
  "activity":-1500000,"to_be_budgeted":0,"age_of_money":34,"deleted":false,
  "categories":[
    {"id":"11111111-1111-1111-1111-111111111111","category_group_id":"aaaa",
     "category_group_name":"Immediate Obligations","name":"Rent","hidden":false,
     "internal":false,"note":null,"budgeted":1200000,"activity":-1200000,
     "balance":0,"balance_formatted":"$0.00","balance_currency":0.0,
     "goal_type":"NEED","goal_target":1200000,"goal_target_month":null,"deleted":false},
    {"id":"22222222-2222-2222-2222-222222222222","category_group_name":"Fun",
     "name":"Dining Out","hidden":false,"internal":false,
     "balance":-45230,"balance_formatted":"-$45.23","deleted":false},
    {"id":"33333333-3333-3333-3333-333333333333","name":"Old Format",
     "hidden":false,"internal":false,"balance":1234560,"deleted":false},
    {"id":"44444444-4444-4444-4444-444444444444","name":"Archived",
     "hidden":true,"internal":false,"balance":500,"balance_formatted":"$0.50","deleted":false},
    {"id":"55555555-5555-5555-5555-555555555555","name":"Removed",
     "hidden":false,"internal":false,"balance":0,"balance_formatted":"$0.00","deleted":true}
  ]}}})";

class YnabMonthParserTest : public ::testing::Test {
 protected:
  void SetUp() override { g_categories.clear(); }
};

TEST_F(YnabMonthParserTest, ExtractsMonthAndCategories) {
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, MONTH_JSON);

  EXPECT_FALSE(parser.hasError());
  EXPECT_STREQ(parser.month(), "2026-08-01");
  ASSERT_EQ(g_categories.size(), 5u);
  EXPECT_EQ(parser.categoryCount(), 5u);

  EXPECT_EQ(g_categories[0].id, "11111111-1111-1111-1111-111111111111");
  EXPECT_EQ(g_categories[0].name, "Rent");
  EXPECT_EQ(g_categories[0].balanceFormatted, "$0.00");
  EXPECT_EQ(g_categories[0].balanceMilli, 0);
  EXPECT_FALSE(g_categories[0].hidden);
  EXPECT_FALSE(g_categories[0].deleted);
}

TEST_F(YnabMonthParserTest, KeepsNegativeBalances) {
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, MONTH_JSON);

  ASSERT_EQ(g_categories.size(), 5u);
  EXPECT_EQ(g_categories[1].name, "Dining Out");
  EXPECT_EQ(g_categories[1].balanceMilli, -45230);
  EXPECT_EQ(g_categories[1].balanceFormatted, "-$45.23");
}

TEST_F(YnabMonthParserTest, ReportsAbsentFormattedBalance) {
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, MONTH_JSON);

  ASSERT_EQ(g_categories.size(), 5u);
  EXPECT_EQ(g_categories[2].name, "Old Format");
  EXPECT_EQ(g_categories[2].balanceFormatted, "");
  EXPECT_EQ(g_categories[2].balanceMilli, 1234560);
}

TEST_F(YnabMonthParserTest, ReportsHiddenAndDeletedForTheSinkToFilter) {
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, MONTH_JSON);

  ASSERT_EQ(g_categories.size(), 5u);
  EXPECT_TRUE(g_categories[3].hidden);
  EXPECT_FALSE(g_categories[3].deleted);
  EXPECT_FALSE(g_categories[4].hidden);
  EXPECT_TRUE(g_categories[4].deleted);
}

// The month object's own "deleted" and "month" fields sit at the same nesting
// as the categories array; neither must leak into the first category.
TEST_F(YnabMonthParserTest, MonthLevelFieldsDoNotLeakIntoCategories) {
  const char* const json = R"({"data":{"month":{"month":"2026-01-01","deleted":true,
    "categories":[{"id":"abc","name":"First","balance":1000,"balance_formatted":"$1.00","hidden":false,"deleted":false}]}}})";
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, json);

  EXPECT_STREQ(parser.month(), "2026-01-01");
  ASSERT_EQ(g_categories.size(), 1u);
  EXPECT_EQ(g_categories[0].name, "First");
  EXPECT_FALSE(g_categories[0].deleted);
}

// Nested objects and arrays inside a category (YNAB has none today, but a new
// field must not end the entry early or swallow the next one).
TEST_F(YnabMonthParserTest, WalksNestedStructuresInsideACategory) {
  const char* const json = R"({"data":{"month":{"month":"2026-03-01","categories":[
    {"id":"one","name":"Nested","balance":5000,"balance_formatted":"$5.00","hidden":false,"deleted":false,
     "meta":{"id":"not-the-category","name":"not-the-name","tags":["a","b"]}},
    {"id":"two","name":"After","balance":7000,"balance_formatted":"$7.00","hidden":false,"deleted":false}
  ]}}})";
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, json);

  EXPECT_FALSE(parser.hasError());
  ASSERT_EQ(g_categories.size(), 2u);
  EXPECT_EQ(g_categories[0].id, "one");
  EXPECT_EQ(g_categories[0].name, "Nested");
  EXPECT_EQ(g_categories[1].id, "two");
  EXPECT_EQ(g_categories[1].name, "After");
}

// An entry with no id cannot be matched against a selection, so it is not
// emitted at all.
TEST_F(YnabMonthParserTest, SkipsEntriesWithoutAnId) {
  const char* const json =
      R"({"data":{"month":{"categories":[{"name":"No id","balance":1},{"id":"x","name":"Kept","balance":2}]}}})";
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, json);

  ASSERT_EQ(g_categories.size(), 1u);
  EXPECT_EQ(g_categories[0].id, "x");
  EXPECT_STREQ(parser.month(), "");
}

TEST_F(YnabMonthParserTest, HandlesAnEmptyCategoryList) {
  const char* const json = R"({"data":{"month":{"month":"2026-05-01","categories":[]}}})";
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, json);

  EXPECT_FALSE(parser.hasError());
  EXPECT_TRUE(g_categories.empty());
  EXPECT_STREQ(parser.month(), "2026-05-01");
}

// Names are cut to the display cap, and the cut lands on a codepoint boundary:
// half a UTF-8 sequence draws as a stray glyph, not as a clipped word. "ab"
// plus 3-byte characters puts the 48-byte cap mid-sequence, which a plain
// memcpy truncation would split.
TEST_F(YnabMonthParserTest, CutsLongNamesOnACodepointBoundary) {
  std::string name = "ab";
  for (int i = 0; i < 20; i++) name += "\xe2\x82\xac";  // U+20AC EURO SIGN

  const std::string json = std::string(R"({"data":{"month":{"categories":[{"id":"x","name":")") + name +
                           R"(","balance":0,"balance_formatted":"$0.00","hidden":false,"deleted":false}]}}})";
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, json);

  ASSERT_EQ(g_categories.size(), 1u);
  const std::string& kept = g_categories[0].name;
  // 2 ASCII bytes plus as many whole 3-byte characters as fit under 48.
  EXPECT_EQ(kept.size(), 2u + 15u * 3u);
  EXPECT_EQ(kept, name.substr(0, kept.size()));
}

TEST_F(YnabMonthParserTest, ResetClearsEverything) {
  YnabMonthParser parser(categorySink, nullptr);
  feedInChunks(parser, MONTH_JSON);
  ASSERT_FALSE(g_categories.empty());

  parser.reset();
  g_categories.clear();
  EXPECT_STREQ(parser.month(), "");
  EXPECT_EQ(parser.categoryCount(), 0u);

  feedInChunks(parser, R"({"data":{"month":{"month":"2027-02-01","categories":[
    {"id":"z","name":"Second Run","balance":10,"balance_formatted":"$0.01","hidden":false,"deleted":false}]}}})");
  ASSERT_EQ(g_categories.size(), 1u);
  EXPECT_EQ(g_categories[0].name, "Second Run");
  EXPECT_STREQ(parser.month(), "2027-02-01");
}

}  // namespace
