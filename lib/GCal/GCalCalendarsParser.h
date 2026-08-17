#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style extractor for the Google Calendar "calendarList.list" response:
 *
 *   {"items":[{"id":"a@group.calendar.google.com","summary":"Work",
 *              "primary":true,"selected":true,...}],...}
 *
 * Only id and summary are kept. Streamed rather than buffered because each
 * entry carries conferenceProperties, notificationSettings, reminder arrays and
 * colour fields, so a user with a dozen calendars can produce tens of KB - too
 * much to hold whole beside a live TLS session.
 */
class GCalCalendarsParser {
 public:
  // Invoked once per calendar object, as soon as it closes.
  using CalendarSink = void (*)(void* ctx, const char* id, const char* summary, bool primary);

  GCalCalendarsParser(CalendarSink sink, void* sinkCtx);

  GCalCalendarsParser(const GCalCalendarsParser&) = delete;
  GCalCalendarsParser& operator=(const GCalCalendarsParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  size_t calendarCount() const { return calendarsSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ITEMS_ARRAY,
    IN_CALENDAR_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    ITEMS,
    CAL_ID,
    CAL_SUMMARY,
    CAL_PRIMARY,
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

  void commitCalendar();

  StreamingJsonParser parser;
  CalendarSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t calDepth;  // Nesting inside the current calendar (1 = the entry itself)
  size_t calendarsSeen;

  char currentId[129];
  char currentSummary[65];
  bool currentPrimary;
};
