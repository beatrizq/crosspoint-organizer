#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CivilTime.h"
#include "GCalCalendarsParser.h"
#include "GCalEventsParser.h"

namespace {

struct ParsedEvent {
  std::string summary;
  std::string start;
  std::string end;
  std::string status;
};

std::vector<ParsedEvent> g_events;
void eventSink(void*, const char* summary, const char* start, const char* end, const char* status) {
  g_events.push_back({summary, start, end, status});
}

struct ParsedCalendar {
  std::string id;
  std::string summary;
  bool primary;
};

std::vector<ParsedCalendar> g_calendars;
void calendarSink(void*, const char* id, const char* summary, const bool primary) {
  g_calendars.push_back({id, summary, primary});
}

// Feeds the document in irregular chunks, which is how it arrives off a TLS
// socket. A parser that only works on whole buffers passes the naive test and
// fails on the device.
void feedInChunks(GCalEventsParser& parser, const std::string& json) {
  size_t offset = 0;
  while (offset < json.size()) {
    size_t n = (offset % 7) + 1;
    if (offset + n > json.size()) n = json.size() - offset;
    parser.feed(json.data() + offset, n);
    offset += n;
  }
}

// A response exercising every shape the screen has to survive: a timed event, an
// all-day event, a cancelled instance, an event with no summary, and a sibling
// object (originalStartTime) that carries its own "date" key.
const char* const EVENTS_JSON = R"({"kind":"calendar#events","items":[
  {"summary":"Standup","status":"confirmed",
   "start":{"dateTime":"2026-08-17T09:30:00+01:00","timeZone":"Europe/Lisbon"},
   "end":{"dateTime":"2026-08-17T10:00:00+01:00"}},
  {"summary":"Holiday","status":"confirmed",
   "start":{"date":"2026-08-20"},"end":{"date":"2026-08-21"}},
  {"summary":"Gone","status":"cancelled",
   "originalStartTime":{"date":"2026-08-18"},
   "start":{"dateTime":"2026-08-19T14:00:00Z"},"end":{"dateTime":"2026-08-19T15:00:00Z"}},
  {"status":"confirmed","attendees":[{"email":"a@b.c","responseStatus":"accepted"}],
   "start":{"dateTime":"2026-08-21T08:05:00Z"},"end":{"dateTime":"2026-08-21T08:35:00Z"}}
],"nextPageToken":"tok"})";

}  // namespace

class GCalEvents : public ::testing::Test {
 protected:
  void SetUp() override { g_events.clear(); }
};

TEST_F(GCalEvents, ParsesEveryEventShape) {
  GCalEventsParser parser(eventSink, nullptr);
  feedInChunks(parser, EVENTS_JSON);
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(4u, g_events.size());
  EXPECT_EQ(4u, parser.eventCount());
}

TEST_F(GCalEvents, ReadsTimedEvent) {
  GCalEventsParser parser(eventSink, nullptr);
  feedInChunks(parser, EVENTS_JSON);
  ASSERT_EQ(4u, g_events.size());
  EXPECT_EQ("Standup", g_events[0].summary);
  EXPECT_EQ("2026-08-17T09:30:00+01:00", g_events[0].start);
  EXPECT_EQ("confirmed", g_events[0].status);
}

TEST_F(GCalEvents, ReadsAllDayEventAsAPlainDate) {
  GCalEventsParser parser(eventSink, nullptr);
  feedInChunks(parser, EVENTS_JSON);
  ASSERT_EQ(4u, g_events.size());
  EXPECT_EQ("Holiday", g_events[1].summary);
  EXPECT_EQ("2026-08-20", g_events[1].start);
  // No time half is what marks it all-day downstream.
  EXPECT_EQ(civil::NO_TIME, civil::timeFromRfc3339(g_events[1].start.c_str()));
}

TEST_F(GCalEvents, SiblingObjectWithItsOwnDateDoesNotOverwriteStart) {
  // originalStartTime appears before start and carries a "date" key. If the
  // depth tracking is wrong, that value lands in the event's start.
  GCalEventsParser parser(eventSink, nullptr);
  feedInChunks(parser, EVENTS_JSON);
  ASSERT_EQ(4u, g_events.size());
  EXPECT_EQ("cancelled", g_events[2].status);
  EXPECT_EQ("2026-08-19T14:00:00Z", g_events[2].start);
}

TEST_F(GCalEvents, EventWithoutSummaryStillReportsItsStart) {
  GCalEventsParser parser(eventSink, nullptr);
  feedInChunks(parser, EVENTS_JSON);
  ASSERT_EQ(4u, g_events.size());
  EXPECT_EQ("", g_events[3].summary);
  EXPECT_EQ("2026-08-21T08:05:00Z", g_events[3].start);
}

TEST_F(GCalEvents, EmptyItemsArrayYieldsNothing) {
  GCalEventsParser parser(eventSink, nullptr);
  const std::string json = R"({"kind":"calendar#events","items":[]})";
  parser.feed(json.data(), json.size());
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(0u, g_events.size());
}

class GCalCalendars : public ::testing::Test {
 protected:
  void SetUp() override { g_calendars.clear(); }
};

TEST_F(GCalCalendars, ParsesIdSummaryAndPrimaryPastNestedFields) {
  const std::string json = R"({"items":[
    {"id":"primary@gmail.com","summary":"Personal","primary":true,
     "conferenceProperties":{"allowedConferenceSolutionTypes":["hangoutsMeet"]}},
    {"id":"work@group.calendar.google.com","summary":"Work",
     "defaultReminders":[{"method":"popup","minutes":10}]},
    {"id":"nosummary@x.com"}
  ]})";
  GCalCalendarsParser parser(calendarSink, nullptr);
  parser.feed(json.data(), json.size());
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(3u, g_calendars.size());

  EXPECT_EQ("primary@gmail.com", g_calendars[0].id);
  EXPECT_EQ("Personal", g_calendars[0].summary);
  EXPECT_TRUE(g_calendars[0].primary);

  EXPECT_EQ("Work", g_calendars[1].summary);
  EXPECT_FALSE(g_calendars[1].primary);

  EXPECT_EQ("nosummary@x.com", g_calendars[2].id);
  EXPECT_EQ("", g_calendars[2].summary);
}

TEST(CivilTimeRfc3339, ExtractsWallClockMinutes) {
  EXPECT_EQ(9 * 60 + 30, civil::timeFromRfc3339("2026-08-17T09:30:00+01:00"));
  EXPECT_EQ(8 * 60 + 5, civil::timeFromRfc3339("2026-08-21T08:05:00Z"));
  EXPECT_EQ(0, civil::timeFromRfc3339("2026-08-21T00:00:00Z"));
  EXPECT_EQ(23 * 60 + 59, civil::timeFromRfc3339("2026-08-21T23:59:00Z"));
  // A plain date has no time half.
  EXPECT_EQ(civil::NO_TIME, civil::timeFromRfc3339("2026-08-20"));
  EXPECT_EQ(civil::NO_TIME, civil::timeFromRfc3339("2026-08-20T25:00:00Z"));
  EXPECT_EQ(civil::NO_TIME, civil::timeFromRfc3339("2026-08-20T09:60:00Z"));
  EXPECT_EQ(civil::NO_TIME, civil::timeFromRfc3339(nullptr));
}

TEST(CivilTimeHttpDate, ParsesTheResponseHeaderClock) {
  // This header is the device's clock; most boards have no RTC.
  EXPECT_EQ(civil::dateFromIso("2026-08-17"), civil::dateFromHttpHeader("Sun, 17 Aug 2026 16:47:35 GMT"));
  EXPECT_EQ(civil::dateFromIso("2027-01-01"), civil::dateFromHttpHeader("Mon, 01 Jan 2027 00:00:00 GMT"));
  EXPECT_EQ(civil::dateFromIso("2026-12-31"), civil::dateFromHttpHeader("Thu, 31 Dec 2026 23:59:59 GMT"));
  EXPECT_EQ(civil::NO_DATE, civil::dateFromHttpHeader("garbage"));
  EXPECT_EQ(civil::NO_DATE, civil::dateFromHttpHeader("Sun, 17 Xxx 2026 16:47:35 GMT"));
  EXPECT_EQ(civil::NO_DATE, civil::dateFromHttpHeader(nullptr));
  EXPECT_EQ(civil::NO_DATE, civil::dateFromHttpHeader(""));
}

TEST(CivilTimeRfc3339, RendersTheApiTimeWindow) {
  char buf[21];
  civil::rfc3339FromDate(civil::dateFromIso("2026-08-17"), buf, sizeof(buf));
  EXPECT_STREQ("2026-08-17T00:00:00Z", buf);

  civil::rfc3339FromDate(civil::NO_DATE, buf, sizeof(buf));
  EXPECT_STREQ("", buf);
}

TEST(CivilTimeCalendar, WeekdayAndComponentsMatchKnownDates) {
  // 2026-08-17 is a Monday; 0 = Sunday.
  EXPECT_EQ(1, civil::weekdayFromDate(civil::dateFromIso("2026-08-17")));
  EXPECT_EQ(0, civil::weekdayFromDate(civil::dateFromIso("2026-08-16")));
  EXPECT_EQ(6, civil::weekdayFromDate(civil::dateFromIso("2026-08-22")));

  const uint16_t d = civil::dateFromIso("2026-08-17");
  EXPECT_EQ(8, civil::monthFromDate(d));
  EXPECT_EQ(17, civil::dayOfMonthFromDate(d));
  EXPECT_EQ(0, civil::monthFromDate(civil::NO_DATE));
}
