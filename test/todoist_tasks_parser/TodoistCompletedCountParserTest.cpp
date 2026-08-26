#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "lib/Todoist/TodoistCompletedCountParser.h"

namespace {

// Trimmed but structurally faithful GET
// /api/v1/tasks/completed/by_completion_date response: items carry nested
// objects/arrays (due, labels) the parser must walk past without miscounting.
const char* kRealisticResponse = R"({
  "items": [
    {
      "id": "6X4Vw2Hfmg73Q2XR",
      "content": "terminar fixes cup pong para release",
      "project_id": "220474322",
      "labels": ["urgent", "work"],
      "due": {"date": "2026-08-17", "is_recurring": false},
      "completed_at": "2026-08-17T14:30:00Z"
    },
    {
      "id": "6X4Vw2Hfmg73Q2XS",
      "content": "confirmar q cambios hacer a goat para crazygames",
      "due": null,
      "labels": [],
      "completed_at": "2026-08-17T09:12:00Z"
    },
    {
      "id": "6X4Vw2Hfmg73Q2XT",
      "content": "hacer musicas pong",
      "completed_at": "2026-08-17T23:59:00Z"
    }
  ],
  "next_cursor": null
})";

size_t countInChunks(const char* body, const size_t chunkSize) {
  TodoistCompletedCountParser parser;
  const size_t len = strlen(body);
  for (size_t offset = 0; offset < len; offset += chunkSize) {
    parser.feed(body + offset, std::min(chunkSize, len - offset));
  }
  EXPECT_FALSE(parser.hasError());
  return parser.count();
}

}  // namespace

TEST(TodoistCompletedCountParserTest, CountsEveryItemAtOnce) {
  EXPECT_EQ(countInChunks(kRealisticResponse, 1u << 20), 3u);
}

TEST(TodoistCompletedCountParserTest, CountsAcrossByteChunks) {
  // A single byte at a time forces the parser to resume mid-token, mid-object
  // and mid-array on nearly every feed() call.
  EXPECT_EQ(countInChunks(kRealisticResponse, 1), 3u);
}

TEST(TodoistCompletedCountParserTest, EmptyItemsArray) {
  EXPECT_EQ(countInChunks(R"({"items":[],"next_cursor":null})", 1u << 20), 0u);
}

TEST(TodoistCompletedCountParserTest, NestingOverflowReportsError) {
  // The underlying StreamingJsonParser flags hasError() on exceeding
  // MAX_NESTING, not on merely truncated input (more bytes could always
  // still arrive) - matches StreamingJsonParserTest's own NestingOverflow case.
  std::string body;
  for (size_t i = 0; i < StreamingJsonParser::MAX_NESTING + 5; ++i) body += "[";

  TodoistCompletedCountParser parser;
  parser.feed(body.c_str(), body.size());
  EXPECT_TRUE(parser.hasError());
}

TEST(TodoistCompletedCountParserTest, ResetClearsCountAndError) {
  TodoistCompletedCountParser parser;
  parser.feed(kRealisticResponse, strlen(kRealisticResponse));
  EXPECT_EQ(parser.count(), 3u);

  parser.reset();
  EXPECT_EQ(parser.count(), 0u);
  EXPECT_FALSE(parser.hasError());

  const char* body = R"({"items":[{"id":"x"}]})";
  parser.feed(body, strlen(body));
  EXPECT_EQ(parser.count(), 1u);
}
