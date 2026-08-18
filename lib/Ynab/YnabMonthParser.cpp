#include "YnabMonthParser.h"

#include <Utf8.h>

#include <cstdlib>
#include <cstring>

namespace {

// Copies at most dstSize-1 bytes, cutting at a codepoint boundary: category
// names are user-written and routinely accented, and half a UTF-8 sequence
// draws as a stray glyph on the row rather than as a clipped word.
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

YnabMonthParser::YnabMonthParser(const CategorySink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void YnabMonthParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  catDepth = 0;
  categoriesSeen = 0;
  monthIso[0] = '\0';
  clearCurrent();
}

void YnabMonthParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void YnabMonthParser::clearCurrent() {
  currentId[0] = '\0';
  currentName[0] = '\0';
  currentBalanceText[0] = '\0';
  currentBalance = 0;
  currentHidden = false;
  currentDeleted = false;
}

void YnabMonthParser::commitCategory() {
  // An entry with no id cannot be selected or matched against a selection, so
  // it is not a category as far as this screen is concerned.
  if (currentId[0] != '\0') {
    categoriesSeen++;
    if (sink) {
      const YnabParsedCategory category{currentId,      currentName,   currentBalanceText,
                                        currentBalance, currentHidden, currentDeleted};
      sink(sinkCtx, category);
    }
  }
  clearCurrent();
}

// -- SAX callbacks (static trampolines) -------------------------------------

void YnabMonthParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      // "categories" is matched by name rather than by depth: it sits three
      // objects down ({data:{month:{categories:[...]}}}), and pinning the depth
      // would break the day YNAB wraps the payload in anything else. The same
      // key cannot appear elsewhere in this response.
      if (keyIs(key, len, "categories", 10))
        self->lastKey = LastKey::CATEGORIES;
      else if (keyIs(key, len, "month", 5))
        self->lastKey = LastKey::MONTH;
      else
        self->lastKey = LastKey::NONE;
      break;
    case Position::IN_CATEGORIES_ARRAY:
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_CATEGORY_OBJECT:
      if (self->catDepth == 1) {
        if (keyIs(key, len, "id", 2))
          self->lastKey = LastKey::CAT_ID;
        else if (keyIs(key, len, "name", 4))
          self->lastKey = LastKey::CAT_NAME;
        else if (keyIs(key, len, "hidden", 6))
          self->lastKey = LastKey::CAT_HIDDEN;
        else if (keyIs(key, len, "deleted", 7))
          self->lastKey = LastKey::CAT_DELETED;
        else if (keyIs(key, len, "balance", 7))
          self->lastKey = LastKey::CAT_BALANCE;
        else if (keyIs(key, len, "balance_formatted", 17))
          self->lastKey = LastKey::CAT_BALANCE_FORMATTED;
        else
          self->lastKey = LastKey::NONE;
      } else {
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void YnabMonthParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  if (self->position == Position::TOP_LEVEL) {
    // The month object's own "month" field, e.g. "2026-08-01". The first one
    // wins: the wrapper key is seen before any nested date-shaped field.
    if (self->lastKey == LastKey::MONTH && self->monthIso[0] == '\0') {
      safeCopy(self->monthIso, sizeof(self->monthIso), value, len);
    }
  } else if (self->position == Position::IN_CATEGORY_OBJECT && self->catDepth == 1) {
    switch (self->lastKey) {
      case LastKey::CAT_ID:
        safeCopy(self->currentId, sizeof(self->currentId), value, len);
        break;
      case LastKey::CAT_NAME:
        safeCopy(self->currentName, sizeof(self->currentName), value, len);
        break;
      case LastKey::CAT_BALANCE_FORMATTED:
        safeCopy(self->currentBalanceText, sizeof(self->currentBalanceText), value, len);
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnNumber(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  if (self->position == Position::IN_CATEGORY_OBJECT && self->catDepth == 1 && self->lastKey == LastKey::CAT_BALANCE) {
    // Milliunits are integral, but a stray fraction or exponent would still
    // parse to the right whole part rather than aborting the walk.
    char buf[24];
    safeCopy(buf, sizeof(buf), value, len);
    self->currentBalance = strtoll(buf, nullptr, 10);
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  if (self->position == Position::IN_CATEGORY_OBJECT && self->catDepth == 1) {
    if (self->lastKey == LastKey::CAT_HIDDEN) {
      self->currentHidden = value;
    } else if (self->lastKey == LastKey::CAT_DELETED) {
      self->currentDeleted = value;
    }
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnNull(void* ctx) { static_cast<YnabMonthParser*>(ctx)->lastKey = LastKey::NONE; }

void YnabMonthParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_CATEGORIES_ARRAY:
      self->position = Position::IN_CATEGORY_OBJECT;
      self->catDepth = 1;
      self->clearCurrent();
      break;
    case Position::IN_CATEGORY_OBJECT:
      self->catDepth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_CATEGORY_OBJECT:
      self->catDepth--;
      if (self->catDepth == 0) {
        self->commitCategory();
        self->position = Position::IN_CATEGORIES_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::CATEGORIES) {
        self->position = Position::IN_CATEGORIES_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_CATEGORY_OBJECT:
      self->catDepth++;
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void YnabMonthParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<YnabMonthParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_CATEGORIES_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_CATEGORY_OBJECT:
      self->catDepth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}
