#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

#include "HabitifyHabit.h"

/** One habit, as it appears while the response is being walked. */
struct HabitifyParsedHabit {
  const char* id;
  const char* name;
  const char* unitSymbol;  // progress.unit; "" when the habit has no goal
  float current;           // progress.current
  float target;            // progress.target
  // Habitify's own verdict for the day - "status" == "completed". The only
  // signal available for a goal-less (target == 0) habit, where current/target
  // can never say so; also true for a numeric habit the user marked done in
  // Habitify itself without necessarily reaching the target through this app.
  bool completed;
};

/**
 * SAX-style extractor for the Habitify "get habit journal" response:
 *
 *   {"data":[{"id":"...","name":"Read","status":"inprogress","type":"good",
 *             "currentStreak":{"length":7,"unit":"day"},
 *             "progress":{"current":1,"target":3,"unit":"rep",
 *                         "periodicity":"daily"},
 *             "logInfo":{"type":"manual"}, ...}]}
 *
 * Only what the row draws (and what marks a habit done) survives: the id
 * (needed to log against), the name, the three progress fields behind "x/y",
 * and status. Colour, icon, streak, areas, reminders and goals are dropped at
 * parse time.
 *
 * Streamed rather than buffered because a habit carries a dozen nested objects
 * it does not need - reminders, end conditions, goals, areas - and forty of them
 * runs to tens of KB, far too much to hold whole beside a live TLS session on a
 * device with ~90KB of free heap.
 *
 * Written rather than reusing YnabRecordParser for two reasons: the array sits
 * directly under "data" rather than under a named key inside it, and the fields
 * that matter are one level deeper, inside "progress" - which that parser
 * deliberately ignores so a nested object's keys cannot be mistaken for the
 * record's own.
 */
class HabitifyJournalParser {
 public:
  // Invoked once per habit object, as soon as it closes.
  using HabitSink = void (*)(void* ctx, const HabitifyParsedHabit& habit);

  HabitifyJournalParser(HabitSink sink, void* sinkCtx);

  HabitifyJournalParser(const HabitifyJournalParser&) = delete;
  HabitifyJournalParser& operator=(const HabitifyJournalParser&) = delete;

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
    IN_PROGRESS_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    DATA,
    HABIT_ID,
    HABIT_NAME,
    HABIT_STATUS,
    PROGRESS,
    PROGRESS_CURRENT,
    PROGRESS_TARGET,
    PROGRESS_UNIT,
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
  HabitSink sink;
  void* sinkCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;          // Object/array nesting outside the habits array
  uint8_t habitDepth;     // Nesting inside the current habit (1 = the entry itself)
  uint8_t progressDepth;  // Nesting inside "progress" (1 = the object itself)
  size_t habitsSeen;

  // Sized from the display caps, so the walk truncates once and nothing
  // downstream has to cut a string again.
  char currentId[HABITIFY_HABIT_ID_MAX_LEN + 1];
  char currentName[HabitifyHabit::NAME_MAX_LEN + 1];
  char currentUnit[HabitifyHabit::UNIT_MAX_LEN + 1];
  float currentValue;
  float currentTarget;
  bool currentCompleted;
};
