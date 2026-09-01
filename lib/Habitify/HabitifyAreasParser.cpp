#include "HabitifyAreasParser.h"

#include <Utf8.h>

#include <cstring>

namespace {

// Same codepoint-safe truncation as HabitifyJournalParser's own safeCopy():
// area names, like habit names, are user-written and routinely carry accents
// or emoji.
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  if (n < srcLen) n = static_cast<size_t>(utf8SafeTruncateBuffer(src, static_cast<int>(n)));
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* expected, size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}

}  // namespace

HabitifyAreasParser::HabitifyAreasParser(const HabitAreaSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void HabitifyAreasParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  habitDepth = 0;
  areaObjDepth = 0;
  habitsSeen = 0;
  clearCurrent();
}

void HabitifyAreasParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void HabitifyAreasParser::clearCurrent() {
  currentHabitId[0] = '\0';
  firstAreaId[0] = '\0';
  firstAreaName[0] = '\0';
  scratchAreaId[0] = '\0';
  scratchAreaName[0] = '\0';
}

void HabitifyAreasParser::commitArea() {
  // Only the first area for this habit is kept -- see the header comment.
  if (firstAreaId[0] == '\0' && scratchAreaId[0] != '\0') {
    safeCopy(firstAreaId, sizeof(firstAreaId), scratchAreaId, strlen(scratchAreaId));
    safeCopy(firstAreaName, sizeof(firstAreaName), scratchAreaName, strlen(scratchAreaName));
  }
  scratchAreaId[0] = '\0';
  scratchAreaName[0] = '\0';
}

void HabitifyAreasParser::commitHabit() {
  // An entry with no id cannot be joined back onto the journal's habits by
  // id, so it is not a habit as far as this parser is concerned.
  if (currentHabitId[0] != '\0') {
    habitsSeen++;
    if (sink) sink(sinkCtx, currentHabitId, firstAreaId, firstAreaName);
  }
  clearCurrent();
}

// -- SAX callbacks (static trampolines) -------------------------------------

void HabitifyAreasParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = keyIs(key, len, "data", 4) ? LastKey::DATA : LastKey::NONE;
      break;
    case Position::IN_HABITS_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_HABIT_OBJECT:
      // Only the habit's own direct fields -- anything deeper (progress,
      // currentStreak, an area's own nested fields) is walked past via the
      // generic depth counters below.
      if (self->habitDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::HABIT_ID;
        else if (keyIs(key, len, "areas", 5))
          self->lastKey = LastKey::AREAS;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_AREAS_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_AREA_OBJECT:
      if (self->areaObjDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::AREA_ID;
        else if (keyIs(key, len, "name", 4))
          self->lastKey = LastKey::AREA_NAME;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void HabitifyAreasParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

  if (self->position == Position::IN_HABIT_OBJECT && self->habitDepth == 1 && self->lastKey == LastKey::HABIT_ID) {
    safeCopy(self->currentHabitId, sizeof(self->currentHabitId), value, len);
  } else if (self->position == Position::IN_AREA_OBJECT && self->areaObjDepth == 1) {
    if (self->lastKey == LastKey::AREA_ID) {
      safeCopy(self->scratchAreaId, sizeof(self->scratchAreaId), value, len);
    } else if (self->lastKey == LastKey::AREA_NAME) {
      safeCopy(self->scratchAreaName, sizeof(self->scratchAreaName), value, len);
    }
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<HabitifyAreasParser*>(ctx)->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<HabitifyAreasParser*>(ctx)->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnNull(void* ctx) { static_cast<HabitifyAreasParser*>(ctx)->lastKey = LastKey::NONE; }

void HabitifyAreasParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

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
      // Any nested object other than an area entry (progress, currentStreak,
      // ...) -- walked past via the generic depth counter, same as
      // HabitifyJournalParser does for the fields it does not want either.
      self->habitDepth++;
      break;
    case Position::IN_AREAS_ARRAY:
      self->position = Position::IN_AREA_OBJECT;
      self->areaObjDepth = 1;
      self->scratchAreaId[0] = '\0';
      self->scratchAreaName[0] = '\0';
      break;
    case Position::IN_AREA_OBJECT:
      self->areaObjDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

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
    case Position::IN_AREA_OBJECT:
      self->areaObjDepth--;
      if (self->areaObjDepth == 0) {
        self->commitArea();
        self->position = Position::IN_AREAS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::DATA) {
        self->position = Position::IN_HABITS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_HABIT_OBJECT:
      if (self->habitDepth == 1 && self->lastKey == LastKey::AREAS) {
        self->position = Position::IN_AREAS_ARRAY;
      } else {
        // Some other nested array (e.g. timeOfDayIds) -- walked past.
        self->habitDepth++;
      }
      break;
    case Position::IN_AREA_OBJECT:
      self->areaObjDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void HabitifyAreasParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<HabitifyAreasParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_HABITS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_HABIT_OBJECT:
      if (self->habitDepth > 0) self->habitDepth--;
      break;
    case Position::IN_AREAS_ARRAY:
      // Back to the habit that owns it, still at its own depth 1.
      self->position = Position::IN_HABIT_OBJECT;
      break;
    case Position::IN_AREA_OBJECT:
      if (self->areaObjDepth > 0) self->areaObjDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
