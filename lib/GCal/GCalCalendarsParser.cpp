#include "GCalCalendarsParser.h"

#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* expected, size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}

}  // namespace

GCalCalendarsParser::GCalCalendarsParser(const CalendarSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void GCalCalendarsParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  calDepth = 0;
  calendarsSeen = 0;
  currentId[0] = '\0';
  currentSummary[0] = '\0';
  currentPrimary = false;
}

void GCalCalendarsParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void GCalCalendarsParser::commitCalendar() {
  if (currentId[0] != '\0') {
    calendarsSeen++;
    if (sink) sink(sinkCtx, currentId, currentSummary, currentPrimary);
  }
  currentId[0] = '\0';
  currentSummary[0] = '\0';
  currentPrimary = false;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void GCalCalendarsParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = (self->depth == 1 && keyIs(key, len, "items", 5)) ? LastKey::ITEMS : LastKey::NONE;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_CALENDAR_OBJECT:
      if (self->calDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::CAL_ID;
        else if (keyIs(key, len, "summary", 7))
          self->lastKey = LastKey::CAL_SUMMARY;
        else if (keyIs(key, len, "primary", 7))
          self->lastKey = LastKey::CAL_PRIMARY;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void GCalCalendarsParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  if (self->position == Position::IN_CALENDAR_OBJECT && self->calDepth == 1) {
    switch (self->lastKey) {
      case LastKey::CAL_ID:
        safeCopy(self->currentId, sizeof(self->currentId), value, len);
        break;
      case LastKey::CAL_SUMMARY:
        safeCopy(self->currentSummary, sizeof(self->currentSummary), value, len);
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<GCalCalendarsParser*>(ctx)->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);
  if (self->position == Position::IN_CALENDAR_OBJECT && self->calDepth == 1 && self->lastKey == LastKey::CAL_PRIMARY) {
    self->currentPrimary = value;
  }
  self->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnNull(void* ctx) { static_cast<GCalCalendarsParser*>(ctx)->lastKey = LastKey::NONE; }

void GCalCalendarsParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::IN_CALENDAR_OBJECT;
      self->calDepth = 1;
      self->currentId[0] = '\0';
      self->currentSummary[0] = '\0';
      self->currentPrimary = false;
      break;
    case Position::IN_CALENDAR_OBJECT:
      self->calDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_CALENDAR_OBJECT:
      self->calDepth--;
      if (self->calDepth == 0) {
        self->commitCalendar();
        self->position = Position::IN_ITEMS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ITEMS && self->depth == 1) {
        self->position = Position::IN_ITEMS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_CALENDAR_OBJECT:
      self->calDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void GCalCalendarsParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<GCalCalendarsParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_CALENDAR_OBJECT:
      self->calDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
