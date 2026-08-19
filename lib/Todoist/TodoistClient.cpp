#include "TodoistClient.h"

#include <CivilTime.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "TodoistStore.h"
#include "TodoistTasksParser.h"

int TodoistClient::lastHttpCode = 0;

namespace {
constexpr char API_BASE[] = "https://api.todoist.com/api/v1";

// "overdue | due before: +N days", percent-encoded, where N is
// TODOIST_WINDOW_DAYS - so the window is today through today+N-1, counting today
// as day one, the same way GCAL_WINDOW_DAYS reads. Overdue is named explicitly
// even though "due before" already covers it: the two halves are what the Overdue
// and Upcoming tabs are made of, and spelling both out keeps the query readable
// against the screen it feeds.
//
// Built from the constant rather than hardcoded so the window has one definition.
// The %% are snprintf escapes for the literal percent signs of the encoding.
constexpr char FILTER_URL_FORMAT[] =
    "https://api.todoist.com/api/v1/tasks/filter"
    "?query=overdue%%20%%7C%%20due%%20before%%3A%%20%%2B%u%%20days&limit=200";

// Same TLS heap gate as KOReaderSyncClient: the wolfSSL handshake needs working
// heap, and a doomed attempt costs ~15s before it gives up. Free heap and the
// largest contiguous block are checked separately because SP ECC caps the
// single largest allocation at the ~17KB record buffer while the total
// transient footprint is higher.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("TDA", "Insufficient heap for TLS handshake: %u free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_FOR_TLS, maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("Authorization", "Bearer " + TODOIST_STORE.getToken());
  http.addHeader("Accept", "application/json");
}

TodoistClient::Error errorForStatus(const int httpCode) {
  if (httpCode <= 0) return TodoistClient::NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return TodoistClient::OK;
  if (httpCode == 401 || httpCode == 403) return TodoistClient::AUTH_FAILED;
  return TodoistClient::SERVER_ERROR;
}

// Sink context for TodoistTasksParser: collects parsed tasks, packing each due
// date. Overdue is decided by the cache, which needs today - only settled once
// the whole response has been seen.
struct TaskCollector {
  std::vector<TodoistTask>* out;
};

void collectTask(void* ctx, const char* id, const char* content, const char* dueDate) {
  auto* collector = static_cast<TaskCollector*>(ctx);
  auto& out = *collector->out;

  TodoistTask task;
  task.id = id;
  task.content = content;
  task.dueDays = todoist::dueDaysFromIso(dueDate);

  if (out.size() < TODOIST_MAX_TASKS) {
    out.push_back(std::move(task));
    return;
  }

  // At the cap. A month-wide window can match more tasks than the cap holds and
  // the API does not promise an order, so dropping whatever arrives last could
  // discard today's tasks to keep next month's. The latest-due task held so far
  // is evicted instead, which leaves the soonest TODOIST_MAX_TASKS whatever
  // order they arrived in. Linear per task past the cap, over at most 60
  // entries - nothing next to the TLS read that delivered them.
  const auto latest = std::max_element(
      out.begin(), out.end(), [](const TodoistTask& a, const TodoistTask& b) { return a.dueDays < b.dueDays; });
  if (latest != out.end() && task.dueDays < latest->dueDays) *latest = std::move(task);
}
}  // namespace

TodoistClient::Error TodoistClient::fetchTasks(std::vector<TodoistTask>& outTasks, std::string& outServerDate) {
  lastHttpCode = 0;
  outServerDate.clear();
  if (!TODOIST_STORE.hasToken()) {
    LOG_DBG("TDA", "No API token configured");
    return NO_TOKEN;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  outTasks.clear();
  outTasks.reserve(TODOIST_MAX_TASKS);
  TaskCollector collector{&outTasks};
  TodoistTasksParser parser(collectTask, &collector);

  char url[160];
  snprintf(url, sizeof(url), FILTER_URL_FORMAT, static_cast<unsigned>(TODOIST_WINDOW_DAYS));

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad filter URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);

  // Streamed into the parser as it arrives: a 200-task response is ~100KB of
  // JSON, which would not fit alongside a live TLS session.
  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });

  // Read before end(): the parsed headers belong to this connection.
  const std::string dateHeader = http.getHeader("date");
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Filter response: %d (%zu tasks parsed)", httpCode, parser.taskCount());

  const Error status = errorForStatus(httpCode);
  if (status != OK) {
    outTasks.clear();
    return status;
  }
  if (parser.hasError()) {
    LOG_ERR("TDA", "Malformed task JSON");
    outTasks.clear();
    return PARSE_ERROR;
  }

  // Today comes off the Date header. Unlike the newest due date this filter can
  // no longer stand in for it - the window reaches a month ahead - and a header
  // present on every successful response beats a body that has to contain
  // something due today to say anything at all.
  const uint16_t serverDate = dateHeader.empty() ? civil::NO_DATE : civil::dateFromHttpHeader(dateHeader.c_str());
  if (serverDate != civil::NO_DATE) {
    char iso[11];
    civil::isoFromDate(serverDate, iso, sizeof(iso));
    outServerDate = iso;
    LOG_DBG("TDA", "Server date: %s", iso);
  } else {
    LOG_DBG("TDA", "No usable Date header; today not derivable from the response");
  }
  return OK;
}

TodoistClient::Error TodoistClient::closeTask(const std::string& taskId) {
  lastHttpCode = 0;
  if (!TODOIST_STORE.hasToken()) return NO_TOKEN;
  if (taskId.empty()) return SERVER_ERROR;
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = std::string(API_BASE) + "/tasks/" + taskId + "/close";
  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("TDA", "Bad close URL for task %s", taskId.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  // The close call takes no body; SecureHttpClient only emits Content-Length
  // for a non-empty payload, and a bodyless POST without it risks a 411.
  http.addHeader("Content-Length", "0");

  const int httpCode = http.sendRequest("POST", nullptr, 0);
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("TDA", "Close %s: %d", taskId.c_str(), httpCode);

  // Already completed (or deleted) elsewhere is the state we wanted anyway.
  if (httpCode == 404) return OK;
  return errorForStatus(httpCode);
}

const char* TodoistClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_TOKEN:
      return "No API token configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Invalid API token";
    case SERVER_ERROR:
      return "Todoist server error";
    case PARSE_ERROR:
      return "Unexpected response";
    case LOW_MEMORY:
      return "Not enough memory to sync";
    default:
      return "Unknown error";
  }
}
