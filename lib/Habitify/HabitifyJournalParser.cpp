#include "HabitifyJournalParser.h"

#include <Utf8.h>

#include <cstdlib>
#include <cstring>

namespace {

// Copies at most dstSize-1 bytes, cutting at a codepoint boundary: habit names
// are user-written and routinely carry accents or emoji, and half a UTF-8
// sequence draws as a stray glyph on the row rather than as a clipped word.
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  if (n < srcLen) n = static_cast<size_t>(utf8SafeTruncateBuffer(src, static_cast<int>(n)));
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* expected, size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}

// Parses a JSON number into a float. The token is copied first because the
// tokenizer hands out a non-terminated view.
float toFloat(const char* value, size_t len) {
  char buf[24];
  safeCopy(buf, sizeof(buf), value, len);
  return strtof(buf, nullptr);
}

}  // namespace

HabitifyJournalParser::HabitifyJournalParser(const HabitSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void HabitifyJournalParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  habitDepth = 0;
  progressDepth = 0;
  habitsSeen = 0;
  clearCurrent();
}

void HabitifyJournalParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void HabitifyJournalParser::clearCurrent() {
  currentId[0] = '\0';
  currentName[0] = '\0';
  currentUnit[0] = '\0';
  currentValue = 0.0f;
  currentTarget = 0.0f;
}

void HabitifyJournalParser::commitHabit() {
  // An entry with no id cannot be logged against or matched to pending progress,
  // so it is not a habit as far as this screen is concerned.
  if (currentId[0] != '\0') {
    habitsSeen++;
    if (sink) {
      const HabitifyParsedHabit habit{currentId, currentName, currentUnit, currentValue, currentTarget};
      sink(sinkCtx, habit);
    }
  }
  clearCurrent();
}

// -- SAX callbacks (static trampolines) -------------------------------------

void HabitifyJournalParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      // "data" is matched by name rather than by depth: it is the only array of
      // habits in the response, and pinning the depth would break the day
      // Habitify wraps the payload in anything else.
      self->lastKey = keyIs(key, len, "data", 4) ? LastKey::DATA : LastKey::NONE;
      break;
    case Position::IN_HABITS_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_HABIT_OBJECT:
      // Only the habit's own fields. Anything deeper is a nested object -
      // currentStreak, reminders, logInfo - whose keys could otherwise collide:
      // currentStreak carries a "unit" too, and taking that as the progress unit
      // would log every habit in days.
      if (self->habitDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::HABIT_ID;
        else if (keyIs(key, len, "name", 4))
          self->lastKey = LastKey::HABIT_NAME;
        else if (keyIs(key, len, "progress", 8))
          self->lastKey = LastKey::PROGRESS;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_PROGRESS_OBJECT:
      if (self->progressDepth == 1) {
        if (keyIs(key, len, "current", 7))
          self->lastKey = LastKey::PROGRESS_CURRENT;
        else if (keyIs(key, len, "target", 6))
          self->lastKey = LastKey::PROGRESS_TARGET;
        else if (keyIs(key, len, "unit", 4))
          self->lastKey = LastKey::PROGRESS_UNIT;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void HabitifyJournalParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  if (self->position == Position::IN_HABIT_OBJECT && self->habitDepth == 1) {
    if (self->lastKey == LastKey::HABIT_ID) {
      safeCopy(self->currentId, sizeof(self->currentId), value, len);
    } else if (self->lastKey == LastKey::HABIT_NAME) {
      safeCopy(self->currentName, sizeof(self->currentName), value, len);
    }
  } else if (self->position == Position::IN_PROGRESS_OBJECT && self->progressDepth == 1 &&
             self->lastKey == LastKey::PROGRESS_UNIT) {
    safeCopy(self->currentUnit, sizeof(self->currentUnit), value, len);
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnNumber(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  if (self->position == Position::IN_PROGRESS_OBJECT && self->progressDepth == 1) {
    if (self->lastKey == LastKey::PROGRESS_CURRENT) {
      self->currentValue = toFloat(value, len);
    } else if (self->lastKey == LastKey::PROGRESS_TARGET) {
      self->currentTarget = toFloat(value, len);
    }
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnBool(void* ctx, bool) {
  static_cast<HabitifyJournalParser*>(ctx)->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnNull(void* ctx) {
  // A null leaves the field at its default, which is what an absent one gives.
  // "icon" and "description" are routinely null, so this is a common path.
  static_cast<HabitifyJournalParser*>(ctx)->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_HABITS_ARRAY:
      self->position = Position::IN_HABIT_OBJECT;
      self->habitDepth = 1;
      self->clearCurrent();
      break;
    case Position::IN_HABIT_OBJECT:
      if (self->habitDepth == 1 && self->lastKey == LastKey::PROGRESS) {
        self->position = Position::IN_PROGRESS_OBJECT;
        self->progressDepth = 1;
      } else {
        self->habitDepth++;
      }
      break;
    case Position::IN_PROGRESS_OBJECT:
      self->progressDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_HABIT_OBJECT:
      self->habitDepth--;
      if (self->habitDepth == 0) {
        self->commitHabit();
        self->position = Position::IN_HABITS_ARRAY;
      }
      break;
    case Position::IN_PROGRESS_OBJECT:
      self->progressDepth--;
      if (self->progressDepth == 0) {
        // Back to the habit that owns it, still at its own depth 1.
        self->position = Position::IN_HABIT_OBJECT;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::DATA) {
        self->position = Position::IN_HABITS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_HABIT_OBJECT:
      self->habitDepth++;
      break;
    case Position::IN_PROGRESS_OBJECT:
      self->progressDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyJournalParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<HabitifyJournalParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_HABITS_ARRAY:
      // The habits array closed; anything after it is top-level again.
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_HABIT_OBJECT:
      if (self->habitDepth > 0) self->habitDepth--;
      break;
    case Position::IN_PROGRESS_OBJECT:
      if (self->progressDepth > 0) self->progressDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
