#include "YnabRecordParser.h"

#include <Utf8.h>

#include <cstdlib>
#include <cstring>

namespace {

// Copies at most dstSize-1 bytes, cutting at a codepoint boundary: payee and
// account names are user-written and routinely accented, and half a UTF-8
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

}  // namespace

YnabRecordParser::YnabRecordParser(const char* arrayKey, const YnabFieldSpec* fieldSpecs, const size_t specCount,
                                   const RecordSink sink, void* sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      arrayKey(arrayKey),
      arrayKeyLen(arrayKey != nullptr ? strlen(arrayKey) : 0),
      fieldCount(specCount < MAX_FIELDS ? specCount : MAX_FIELDS),
      sink(sink),
      sinkCtx(sinkCtx) {
  // Copied rather than held by pointer: the tables are static in the client, but
  // a parser outliving a caller's local table would be a trap for no gain.
  for (size_t i = 0; i < fieldCount; i++) fields[i] = fieldSpecs[i];
  reset();
}

void YnabRecordParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastField = NO_FIELD;
  lastKeyWasArray = false;
  depth = 0;
  recordDepth = 0;
  recordsSeen = 0;
  clearCurrent();
}

void YnabRecordParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void YnabRecordParser::clearCurrent() {
  for (auto& slot : strings) slot[0] = '\0';
  for (auto& slot : numbers) slot = 0;
  for (auto& slot : bools) slot = false;
}

void YnabRecordParser::commitRecord() {
  recordsSeen++;
  if (sink) {
    YnabParsedRecord record{};
    for (size_t i = 0; i < YnabParsedRecord::MAX_STRINGS; i++) record.strings[i] = strings[i];
    for (size_t i = 0; i < YnabParsedRecord::MAX_NUMBERS; i++) record.numbers[i] = numbers[i];
    for (size_t i = 0; i < YnabParsedRecord::MAX_BOOLS; i++) record.bools[i] = bools[i];
    sink(sinkCtx, record);
  }
  clearCurrent();
}

// -- SAX callbacks (static trampolines) -------------------------------------

void YnabRecordParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<YnabRecordParser*>(ctx);
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;

  switch (self->position) {
    case Position::TOP_LEVEL:
      // The array is matched by name rather than by depth: it sits two objects
      // down ({data:{accounts:[...]}}), and pinning the depth would break the day
      // YNAB wraps the payload in anything else. The same key cannot appear
      // elsewhere in these responses.
      self->lastKeyWasArray = keyIs(key, len, self->arrayKey, self->arrayKeyLen);
      break;
    case Position::IN_ARRAY:
      break;
    case Position::IN_RECORD:
      // Only the record's own fields; anything nested is a sub-object whose keys
      // could otherwise collide with one we keep.
      if (self->recordDepth != 1) break;
      for (size_t i = 0; i < self->fieldCount; i++) {
        if (keyIs(key, len, self->fields[i].key, strlen(self->fields[i].key))) {
          self->lastField = static_cast<uint8_t>(i);
          break;
        }
      }
      break;
  }
}

void YnabRecordParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<YnabRecordParser*>(ctx);
  if (self->position == Position::IN_RECORD && self->recordDepth == 1 && self->lastField != NO_FIELD) {
    const YnabFieldSpec& field = self->fields[self->lastField];
    if (field.kind == YnabFieldKind::String && field.slot < YnabParsedRecord::MAX_STRINGS) {
      safeCopy(self->strings[field.slot], STRING_CAPACITY, value, len);
    }
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnNumber(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<YnabRecordParser*>(ctx);
  if (self->position == Position::IN_RECORD && self->recordDepth == 1 && self->lastField != NO_FIELD) {
    const YnabFieldSpec& field = self->fields[self->lastField];
    if (field.kind == YnabFieldKind::Number && field.slot < YnabParsedRecord::MAX_NUMBERS) {
      // Milliunits are integral, but a stray fraction or exponent would still
      // parse to the right whole part rather than aborting the walk.
      char buf[24];
      safeCopy(buf, sizeof(buf), value, len);
      self->numbers[field.slot] = strtoll(buf, nullptr, 10);
    }
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<YnabRecordParser*>(ctx);
  if (self->position == Position::IN_RECORD && self->recordDepth == 1 && self->lastField != NO_FIELD) {
    const YnabFieldSpec& field = self->fields[self->lastField];
    if (field.kind == YnabFieldKind::Bool && field.slot < YnabParsedRecord::MAX_BOOLS) {
      self->bools[field.slot] = value;
    }
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnNull(void* ctx) {
  auto* self = static_cast<YnabRecordParser*>(ctx);
  // A null leaves the slot at its default, which is what an absent field gives.
  // payee_name and memo are routinely null, so this is the common path.
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<YnabRecordParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_ARRAY:
      self->position = Position::IN_RECORD;
      self->recordDepth = 1;
      self->clearCurrent();
      break;
    case Position::IN_RECORD:
      self->recordDepth++;
      break;
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<YnabRecordParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_RECORD:
      self->recordDepth--;
      if (self->recordDepth == 0) {
        self->commitRecord();
        self->position = Position::IN_ARRAY;
      }
      break;
    default:
      break;
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<YnabRecordParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKeyWasArray) {
        self->position = Position::IN_ARRAY;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_RECORD:
      self->recordDepth++;
      break;
    default:
      break;
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}

void YnabRecordParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<YnabRecordParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ARRAY:
      // The record array closed; anything after it is top-level again.
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_RECORD:
      if (self->recordDepth > 0) self->recordDepth--;
      break;
  }
  self->lastField = NO_FIELD;
  self->lastKeyWasArray = false;
}
