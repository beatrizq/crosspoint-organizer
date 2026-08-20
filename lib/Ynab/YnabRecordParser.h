#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/** What kind of value a field carries, and so which slot array it lands in. */
enum class YnabFieldKind : uint8_t { String, Number, Bool };

/** One field worth keeping out of a record. */
struct YnabFieldSpec {
  const char* key;     // JSON key, matched exactly at record depth 1
  YnabFieldKind kind;  // Which slot array `slot` indexes
  uint8_t slot;        // Index into YnabParsedRecord's matching array
};

/**
 * One record's kept fields. Slots the response did not carry read as "" / 0 /
 * false, so a sink cannot tell an absent field from an empty one - which is what
 * every caller here wants anyway.
 */
struct YnabParsedRecord {
  static constexpr size_t MAX_STRINGS = 4;
  static constexpr size_t MAX_NUMBERS = 1;
  static constexpr size_t MAX_BOOLS = 3;

  const char* strings[MAX_STRINGS];
  int64_t numbers[MAX_NUMBERS];
  bool bools[MAX_BOOLS];
};

/**
 * SAX-style extractor for the YNAB list responses shaped
 *
 *   {"data":{"<arrayKey>":[{...},{...}]}}
 *
 * which covers both endpoints the Budget account tabs need:
 *
 *   GET /plans/{id}/accounts                      -> "accounts"
 *   GET /plans/{id}/accounts/{id}/transactions    -> "transactions"
 *
 * Streamed rather than buffered for the same reason YnabMonthParser is: an
 * account carries a dozen fields it does not need and a month of transactions
 * runs to tens of KB, far too much to hold whole beside a live TLS session on a
 * device with ~90KB of free heap.
 *
 * Table-driven because those two responses differ only in which keys matter, and
 * the walk - find the named array, take scalars at depth 1 of each object, emit
 * on close - is identical. YnabMonthParser is left as it is: it also lifts a
 * top-level scalar (the month) out of a wrapper this cannot describe, and
 * reworking a parser that already has tests is not worth the churn.
 *
 * Records are emitted with their `deleted`/`closed` flags intact rather than
 * filtered here; the sink decides, because the accounts screen and the tab want
 * the same walk but not quite the same rows.
 */
class YnabRecordParser {
 public:
  // Per-slot string capacity. Sized for the longest field any caller keeps - a
  // 48-character payee or account name - so the walk truncates once and nothing
  // downstream has to cut a string again.
  static constexpr size_t STRING_CAPACITY = 56;
  static constexpr size_t MAX_FIELDS = 8;

  // Invoked once per record object, as soon as it closes.
  using RecordSink = void (*)(void* ctx, const YnabParsedRecord& record);

  YnabRecordParser(const char* arrayKey, const YnabFieldSpec* fields, size_t fieldCount, RecordSink sink,
                   void* sinkCtx);

  YnabRecordParser(const YnabRecordParser&) = delete;
  YnabRecordParser& operator=(const YnabRecordParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Records seen in the response, including any the sink chose to drop.
  size_t recordCount() const { return recordsSeen; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ARRAY,
    IN_RECORD,
  };

  // Sentinel for "the last key was not one we keep".
  static constexpr uint8_t NO_FIELD = 0xFF;

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
  void commitRecord();

  StreamingJsonParser parser;
  const char* arrayKey;
  size_t arrayKeyLen;
  YnabFieldSpec fields[MAX_FIELDS];
  size_t fieldCount;
  RecordSink sink;
  void* sinkCtx;

  Position position;
  uint8_t lastField;     // Index into `fields`, or NO_FIELD
  bool lastKeyWasArray;  // The key just seen was arrayKey
  uint8_t depth;         // Object/array nesting outside the record array
  uint8_t recordDepth;   // Nesting inside the current record (1 = the entry)
  size_t recordsSeen;

  char strings[YnabParsedRecord::MAX_STRINGS][STRING_CAPACITY];
  int64_t numbers[YnabParsedRecord::MAX_NUMBERS];
  bool bools[YnabParsedRecord::MAX_BOOLS];
};
