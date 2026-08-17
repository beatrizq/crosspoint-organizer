#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style extractor for the Google Calendar "events.list" response:
 *
 *   {"items":[{"summary":"Standup","status":"confirmed",
 *              "start":{"dateTime":"2026-08-17T09:30:00+01:00"},
 *              "end":{"dateTime":"2026-08-17T10:00:00+01:00"}}],...}
 *
 * Only summary, status and the start/end stamps are kept; description,
 * location, attendees, organiser, reminders, conferencing and colour are walked
 * past without being stored. The body is fed in as it arrives off the socket, so
 * a month of events never exists in RAM as a whole - only the ~200 bytes of the
 * event being assembled.
 *
 * All-day events carry `"date"` and timed events `"dateTime"`; both are handed
 * to the sink verbatim in the same argument, and the absence of a time half is
 * what marks an event all-day downstream.
 *
 * The request is issued with singleEvents=true, so recurring events arrive
 * already expanded into individual instances. Nothing here interprets RRULE.
 */
class GCalEventsParser {
 public:
  // Invoked once per event object, as soon as it closes. Any of the strings may
  // be "" when the field was absent.
  using EventSink = void (*)(void* ctx, const char* summary, const char* start, const char* end, const char* status);

  GCalEventsParser(EventSink sink, void* sinkCtx);

  GCalEventsParser(const GCalEventsParser&) = delete;
  GCalEventsParser& operator=(const GCalEventsParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Events seen in the response, including any the sink chose to drop.
  size_t eventCount() const { return eventsSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ITEMS_ARRAY,
    IN_EVENT_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    ITEMS,
    EVENT_SUMMARY,
    EVENT_STATUS,
    EVENT_START,
    EVENT_END,
    STAMP_DATETIME,
    STAMP_DATE,
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

  void commitEvent();

  StreamingJsonParser parser;
  EventSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;       // Object/array nesting outside the items array
  uint8_t eventDepth;  // Nesting inside the current event (1 = the event itself)
  uint8_t startDepth;  // eventDepth of the event's start object; 0 when outside
  uint8_t endDepth;    // eventDepth of the event's end object; 0 when outside
  size_t eventsSeen;

  // RFC3339 with an offset is 25 chars ("2026-08-17T09:30:00+01:00"); an all-day
  // "date" is 10. Statuses are "confirmed", "tentative" or "cancelled".
  char currentSummary[65];
  char currentStart[26];
  char currentEnd[26];
  char currentStatus[12];
};
