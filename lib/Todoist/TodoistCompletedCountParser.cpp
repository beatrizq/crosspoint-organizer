#include "TodoistCompletedCountParser.h"

#include <cstring>

namespace {
bool keyIs(const char* key, const size_t len, const char* expected, const size_t expectedLen) {
  return len == expectedLen && memcmp(key, expected, expectedLen) == 0;
}
}  // namespace

TodoistCompletedCountParser::TodoistCompletedCountParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void TodoistCompletedCountParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKeyWasItems = false;
  depth = 0;
  itemDepth = 0;
  itemCount = 0;
}

void TodoistCompletedCountParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void TodoistCompletedCountParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);
  self->lastKeyWasItems = self->position == Position::TOP_LEVEL && self->depth == 1 && keyIs(key, len, "items", 5);
}

void TodoistCompletedCountParser::sOnString(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnNull(void* ctx) {
  static_cast<TodoistCompletedCountParser*>(ctx)->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::IN_ITEM_OBJECT;
      self->itemDepth = 1;
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth++;
      break;
  }
  self->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth--;
      if (self->itemDepth == 0) {
        self->itemCount++;
        self->position = Position::IN_ITEMS_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKeyWasItems && self->depth == 1) {
        self->position = Position::IN_ITEMS_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth++;
      break;
    default:
      break;
  }
  self->lastKeyWasItems = false;
}

void TodoistCompletedCountParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<TodoistCompletedCountParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ITEMS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ITEM_OBJECT:
      self->itemDepth--;
      break;
  }
  self->lastKeyWasItems = false;
}
