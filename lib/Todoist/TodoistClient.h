#pragma once
#include <string>
#include <vector>

#include "TodoistTask.h"

/**
 * HTTPS client for the Todoist unified API (v1).
 *
 * Base URL: https://api.todoist.com/api/v1
 *
 * Endpoints used:
 *   GET  /tasks/filter?query=today|overdue   - the Today screen's task list
 *   POST /tasks/{id}/close                   - mark a task complete
 *
 * Authentication: Authorization: Bearer <personal API token>
 *
 * Runs over wolfSSL (SecureHttpClient) like the KOReader sync client: Todoist
 * terminates TLS 1.3, which the precompiled system mbedTLS cannot negotiate.
 */
class TodoistClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    PARSE_ERROR,
    LOW_MEMORY,
  };

  /**
   * Fetch the tasks due today plus everything overdue, in one filter query.
   *
   * @param outTasks Output: parsed tasks, capped at TODOIST_MAX_TASKS. Each
   *                 carries its due date packed as TodoistTask::dueDays; the
   *                 overdue flag is left for the cache to set once the caller
   *                 has settled on today's date.
   * @param outDerivedDate Output: the newest due date in the response, as
   *                 "YYYY-MM-DD", or empty when no task carried a due date.
   *                 Because the filter is "today | overdue" the server can only
   *                 return dates up to and including today, so this *is* today
   *                 whenever at least one task is due today - and it is today in
   *                 the account's timezone, which is the one the filter used.
   *                 It reads earlier than today for an all-overdue response, so
   *                 the caller must not let the stored date move backwards.
   */
  static Error fetchTodayTasks(std::vector<TodoistTask>& outTasks, std::string& outDerivedDate);

  /**
   * Complete a task. A 404 is reported as OK: the task is already gone from the
   * server (completed elsewhere), which is the state the caller wants.
   */
  static Error closeTask(const std::string& taskId);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
