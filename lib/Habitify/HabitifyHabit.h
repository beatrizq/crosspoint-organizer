#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * One row of the Habits screen: a habit and how far today has got.
 *
 * `current` and `target` are what the row draws as "x/y". They are floats
 * because Habitify's progress is a float - a distance habit legitimately sits at
 * 2.5 of 5 km - even though the count-based habits this screen is mostly for are
 * whole numbers.
 *
 * `unitSymbol` is Habitify's own unit for the habit ("rep", "min", "kM", ...).
 * It is kept because logging progress has to name the unit: POST .../logs takes
 * a {unitSymbol, value} pair, and the journal response is the only place the
 * habit's unit is known.
 *
 * `pending` is progress added on the device and not yet pushed. It is added to
 * `current` for display, so logging an amount moves the number immediately,
 * and it is sent as a single log entry on the next sync - one request per
 * habit however many separate amounts were logged in between.
 */
struct HabitifyHabit {
  std::string id;          // Habitify habit id, needed for the log push
  std::string name;        // Display name, truncated at parse time
  std::string unitSymbol;  // Unit for logging, e.g. "rep"
  float current = 0.0f;    // Accumulated for the period, as the server last said
  float target = 0.0f;     // Goal for the period; 0 when the habit has no goal
  float pending = 0.0f;    // Added locally, awaiting push
  // Habitify's own "status" == "completed" for the day, as of the last fetch.
  // The only way to know a goal-less habit (no numeric target) is done - see
  // isComplete() - and also true for a numeric habit marked done in Habitify
  // itself in a way this app's current/target comparison would not catch.
  bool completedByStatus = false;
  // Complete tapped locally, awaiting push via Habitify's dedicated
  // .../logs/complete endpoint - independent of `pending`, since completing is
  // "mark done" rather than "add N" and works even for a goal-less habit
  // (no unitSymbol) that `pending`/addLog() cannot touch at all.
  bool pendingComplete = false;

  // What the row shows: the server's figure plus anything not yet pushed.
  float shownCurrent() const { return current + pending; }
  bool hasPending() const { return pending > 0.0f; }
  // A habit with no goal has no "y" to show, so current/target alone cannot
  // judge it - completedByStatus is what does instead.
  bool hasTarget() const { return target > 0.0f; }
  // current/target is checked first (and includes pending) so a numeric
  // habit's row goes bold the instant a press is logged, before the next sync
  // confirms it via status; completedByStatus then covers everything
  // current/target cannot: a goal-less habit, or a numeric one Habitify itself
  // already considers done.
  bool isComplete() const { return (hasTarget() && shownCurrent() >= target) || completedByStatus; }

  static constexpr size_t NAME_MAX_LEN = 64;
  // Longest unit in the API's enum is "fl oz" at five characters.
  static constexpr size_t UNIT_MAX_LEN = 8;
};

// Habits the screen can hold at once. A daily list longer than this has stopped
// being glanceable, and the cap bounds the fetch, the cache file and the parser.
static constexpr size_t HABITIFY_MAX_HABITS = 40;

// A habit id is a UUID-ish string; ids are cut to this length everywhere so one
// stored against pending progress still matches the same habit next sync.
static constexpr size_t HABITIFY_HABIT_ID_MAX_LEN = 48;
