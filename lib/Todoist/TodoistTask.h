#pragma once
#include <string>

// One row of the Today screen. Only what the list renders is kept: due dates,
// projects, labels and priorities are dropped at parse time so a synced list
// costs ~40 bytes plus the content string per task.
struct TodoistTask {
  std::string id;        // Todoist task id, needed for the close call
  std::string content;   // Task title, truncated to CONTENT_MAX_LEN at parse time
  bool overdue = false;  // Due before today; sorted to the top of the list

  static constexpr size_t CONTENT_MAX_LEN = 120;
};

// A Today list longer than this is not readable on a 480px screen anyway; the
// cap bounds the fetch buffer, the SD file, and the completion queue alike.
static constexpr size_t TODOIST_MAX_TASKS = 60;
