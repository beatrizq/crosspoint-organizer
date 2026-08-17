#pragma once
#include <string>

#include "TodoistDate.h"

// One row of the Today screen. Projects, labels and priorities are dropped at
// parse time; the due date survives only as a packed 2-byte day count, so a
// synced list costs ~42 bytes plus the content string per task.
struct TodoistTask {
  std::string id;       // Todoist task id, needed for the close call
  std::string content;  // Task title, truncated to CONTENT_MAX_LEN at parse time
  // Days since 2000-01-01, or todoist::DUE_NONE when the task has no due date.
  // Doubles as the sort key and as the source the sync derives "today" from.
  uint16_t dueDays = todoist::DUE_NONE;
  bool overdue = false;  // Due before today; set by the cache once today is known

  static constexpr size_t CONTENT_MAX_LEN = 120;
};

// A Today list longer than this is not readable on a 480px screen anyway; the
// cap bounds the fetch buffer, the SD file, and the completion queue alike.
static constexpr size_t TODOIST_MAX_TASKS = 60;
