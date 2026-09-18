#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

#include "HabitifyHabit.h"

/**
 * SAX-style extractor for Habitify's "list habits" response's per-habit area
 * assignment:
 *
 *   {"data":[{"id":"...","name":"Read","areas":[{"id":"...","name":"Health",
 *             "colorHex":"...","icon":"...","createdAt":"..."}, ...],
 *             "progress":{...}, "currentStreak":{...}, ...}]}
 *
 * This targets GET /habits, not GET /habits/journal (HabitifyJournalParser's
 * endpoint): per Habitify's own API reference, only /habits (and /habits/
 * {id}) return each habit's "areas" array -- the journal response this app
 * otherwise relies on for progress does not carry it at all. Hence a second
 * fetch (see HabitifyClient::fetchHabitAreas()) joined onto the journal's
 * habits by id, rather than one parser reading both.
 *
 * Only a habit's id and its first area's id/name survive -- a habit can
 * belong to several areas in Habitify itself, but this app shows one tab per
 * habit rather than repeating it across several, so every area after the
 * first is walked past and discarded the same way progress/streak/etc. are.
 * Buffered until the whole habit object closes (like HabitifyJournalParser's
 * own commitHabit()) rather than firing the sink field-by-field, so a habit
 * whose "areas" key happens to arrive before "id" in the response still
 * reports correctly.
 */
class HabitifyAreasParser {
 public:
  // Invoked once per habit object, as soon as it closes. areaId/areaName are
  // "" when the habit belongs to no area.
  using HabitAreaSink = void (*)(void* ctx, const char* habitId, const char* areaId, const char* areaName);

  HabitifyAreasParser(HabitAreaSink sink, void* sinkCtx);

  HabitifyAreasParser(const HabitifyAreasParser&) = delete;
  HabitifyAreasParser& operator=(const HabitifyAreasParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Habits seen in the response, including any the sink chose to drop.
  size_t habitCount() const { return habitsSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_HABITS_ARRAY,
    IN_HABIT_OBJECT,
    IN_AREAS_ARRAY,
    IN_AREA_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    DATA,
    HABIT_ID,
    AREAS,
    AREA_ID,
    AREA_NAME,
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
  void commitHabit();

  StreamingJsonParser parser;
  HabitAreaSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;         // Object/array nesting outside the habits array
  uint8_t habitDepth;    // Nesting inside the current habit (1 = the entry itself)
  uint8_t areaObjDepth;  // Nesting inside the area object currently being read (1 = itself)
  size_t habitsSeen;

  char currentHabitId[HABITIFY_HABIT_ID_MAX_LEN + 1];
  // The first area seen for the current habit; a second one, if any, is
  // parsed into a scratch buffer and discarded (see commitArea()).
  char firstAreaId[HABITIFY_HABIT_ID_MAX_LEN + 1];
  char firstAreaName[HABITIFY_AREA_NAME_MAX_LEN + 1];
  // Scratch for whichever area object is currently being walked.
  char scratchAreaId[HABITIFY_HABIT_ID_MAX_LEN + 1];
  char scratchAreaName[HABITIFY_AREA_NAME_MAX_LEN + 1];

  void commitArea();
};
