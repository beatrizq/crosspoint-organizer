#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "HabitifyJournalParser.h"

namespace {

struct Parsed {
  std::string id;
  std::string name;
  std::string unit;
  float current;
  float target;
};

std::vector<Parsed> g_habits;

void sink(void*, const HabitifyParsedHabit& habit) {
  g_habits.push_back({habit.id, habit.name, habit.unitSymbol, habit.current, habit.target});
}

// Feeds the document in irregular chunks, which is how it arrives off a TLS
// socket. A parser that only works on whole buffers passes the naive test and
// fails on the device.
void feedInChunks(HabitifyJournalParser& parser, const std::string& json) {
  size_t offset = 0;
  while (offset < json.size()) {
    size_t n = (offset % 7) + 1;
    if (offset + n > json.size()) n = json.size() - offset;
    parser.feed(json.data() + offset, n);
    offset += n;
  }
}

class HabitifyJournalParserTest : public ::testing::Test {
 protected:
  void SetUp() override { g_habits.clear(); }
};

// A journal response carrying every shape the screen has to survive: nested
// objects whose keys collide with the ones we keep (currentStreak has a "unit",
// reminders has a "name"), a null icon, a habit part-done, one complete, one with
// no goal at all, and arrays nested inside a habit.
constexpr char JOURNAL_JSON[] = R"({"data":[
  {"id":"h-1","name":"Read 20 pages","status":"inprogress","type":"good","icon":null,
   "colorHex":"#FF6B6B","timeOfDayIds":["morning"],
   "currentStreak":{"length":7,"unit":"day"},
   "progress":{"current":1,"target":3,"unit":"rep","periodicity":"daily"},
   "logInfo":{"type":"manual"}},
  {"id":"h-2","name":"Run","status":"completed","type":"good",
   "currentStreak":{"length":2,"unit":"day"},
   "progress":{"current":5.5,"target":5,"unit":"kM","periodicity":"daily"},
   "reminders":{"timeTriggers":[{"time":{"hour":7,"minute":30},"name":"nested-name"}]},
   "logInfo":{"type":"auto"}},
  {"id":"h-3","name":"Meditate","status":"inprogress","type":"good",
   "progress":{"current":0,"target":0,"unit":"","periodicity":"daily"},
   "areas":[{"id":"a-1","name":"Health"}]}
]})";

TEST_F(HabitifyJournalParserTest, ParsesProgressAndUnits) {
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, JOURNAL_JSON);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_habits.size(), 3u);
  EXPECT_EQ(parser.habitCount(), 3u);

  EXPECT_EQ(g_habits[0].id, "h-1");
  EXPECT_EQ(g_habits[0].name, "Read 20 pages");
  EXPECT_EQ(g_habits[0].unit, "rep");
  EXPECT_FLOAT_EQ(g_habits[0].current, 1.0f);
  EXPECT_FLOAT_EQ(g_habits[0].target, 3.0f);

  // A fractional overshoot: current can exceed target, and both are floats.
  EXPECT_EQ(g_habits[1].name, "Run");
  EXPECT_EQ(g_habits[1].unit, "kM");
  EXPECT_FLOAT_EQ(g_habits[1].current, 5.5f);
  EXPECT_FLOAT_EQ(g_habits[1].target, 5.0f);

  EXPECT_EQ(g_habits[2].name, "Meditate");
  EXPECT_FLOAT_EQ(g_habits[2].target, 0.0f);
}

TEST_F(HabitifyJournalParserTest, NestedUnitDoesNotLeakIntoProgress) {
  // currentStreak carries {"length":7,"unit":"day"}. Taking that "unit" as the
  // progress unit would log every habit in days, so it must not be picked up -
  // and it appears BEFORE progress in the document, which is the dangerous order.
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, JOURNAL_JSON);
  ASSERT_EQ(g_habits.size(), 3u);
  EXPECT_NE(g_habits[0].unit, "day");
  EXPECT_EQ(g_habits[0].unit, "rep");
  EXPECT_NE(g_habits[1].unit, "day");
}

TEST_F(HabitifyJournalParserTest, NestedNameDoesNotOverwriteHabitName) {
  // reminders.timeTriggers[].name would otherwise replace the habit's own name.
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, JOURNAL_JSON);
  ASSERT_EQ(g_habits.size(), 3u);
  EXPECT_EQ(g_habits[1].name, "Run");
}

TEST_F(HabitifyJournalParserTest, SkipsEntriesWithNoId) {
  constexpr char json[] = R"({"data":[{"name":"No id here","progress":{"current":1,"target":2,"unit":"rep"}},)"
                          R"({"id":"h-9","name":"Real","progress":{"current":2,"target":2,"unit":"rep"}}]})";
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, json);
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_habits.size(), 1u);
  EXPECT_EQ(g_habits[0].id, "h-9");
}

TEST_F(HabitifyJournalParserTest, HabitWithNoProgressObjectYieldsZeroes) {
  constexpr char json[] = R"({"data":[{"id":"h-1","name":"Bare"}]})";
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, json);
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_habits.size(), 1u);
  EXPECT_FLOAT_EQ(g_habits[0].current, 0.0f);
  EXPECT_FLOAT_EQ(g_habits[0].target, 0.0f);
  EXPECT_EQ(g_habits[0].unit, "");
}

TEST_F(HabitifyJournalParserTest, EmptyAndPaginatedShapes) {
  HabitifyJournalParser empty(sink, nullptr);
  feedInChunks(empty, R"({"data":[]})");
  EXPECT_FALSE(empty.hasError());
  EXPECT_EQ(g_habits.size(), 0u);

  // A sibling key after the array must not be read as another habit.
  g_habits.clear();
  HabitifyJournalParser paged(sink, nullptr);
  feedInChunks(paged, R"({"data":[{"id":"h-1","name":"A"}],"pagination":{"total":1,"name":"nope"}})");
  EXPECT_FALSE(paged.hasError());
  ASSERT_EQ(g_habits.size(), 1u);
  EXPECT_EQ(g_habits[0].name, "A");
}

TEST_F(HabitifyJournalParserTest, TruncatesLongNamesOnCodepointBoundary) {
  std::string longName;
  while (longName.size() < HabitifyHabit::NAME_MAX_LEN + 8) longName += "é";
  const std::string json = R"({"data":[{"id":"h-1","name":")" + longName + R"("}]})";
  HabitifyJournalParser parser(sink, nullptr);
  feedInChunks(parser, json);
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(g_habits.size(), 1u);
  EXPECT_LE(g_habits[0].name.size(), HabitifyHabit::NAME_MAX_LEN);
  // Every byte pair is a complete two-byte sequence, so the length stays even.
  EXPECT_EQ(g_habits[0].name.size() % 2, 0u);
}

}  // namespace
