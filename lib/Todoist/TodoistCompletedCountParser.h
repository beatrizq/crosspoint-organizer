#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

/**
 * SAX-style counter for the Todoist "completed tasks by completion date"
 * response:
 *
 *   {"items":[{"id":"...","content":"...","completed_at":"...",...}, ...],
 *    "next_cursor":null}
 *
 * Nothing about a completed task is kept - the caller only wants how many
 * items matched, not what they were - so every field is walked past without
 * being stored, the same streaming approach TodoistTasksParser uses.
 */
class TodoistCompletedCountParser {
 public:
  TodoistCompletedCountParser();

  TodoistCompletedCountParser(const TodoistCompletedCountParser&) = delete;
  TodoistCompletedCountParser& operator=(const TodoistCompletedCountParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  size_t count() const { return itemCount; }
  bool hasError() const { return parser.hasError(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ITEMS_ARRAY,
    IN_ITEM_OBJECT,
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

  StreamingJsonParser parser;

  Position position;
  bool lastKeyWasItems;
  uint8_t depth;      // Object/array nesting outside the items array
  uint8_t itemDepth;  // Nesting inside the current item object (1 = the item itself)
  size_t itemCount;
};
