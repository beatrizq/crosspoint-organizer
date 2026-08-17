#include "TodoistClient.h"

#include <Logging.h>
#include <SecureHttpClient.h>

#include <cstring>
#include <utility>

#include "TodoistStore.h"
#include "TodoistTasksParser.h"

int TodoistClient::lastHttpCode = 0;

namespace {
constexpr char API_BASE[] = "https://api.todoist.com/api/v1";

// "today | overdue", percent-encoded. Overdue tasks are wanted in the list
// itself (sorted to the top by the cache), not just in the header count.
constexpr char TODAY_FILTER_URL[] = "https://api.todoist.com/api/v1/tasks/filter?query=today%20%7C%20overdue&limit=200";

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

// Sink context for TodoistTasksParser: appends parsed tasks until the cache cap
// is reached, flagging anything due before today as overdue.
struct TaskCollector {
  std::vector<TodoistTask>* out;
  const char* todayIso;  // "" when the date is unknown; nothing is flagged then
};

void collectTask(void* ctx, const char* id, const char* content, const char* dueDate) {
  auto* collector = static_cast<TaskCollector*>(ctx);
  if (collector->out->size() >= TODOIST_MAX_TASKS) return;

  TodoistTask task;
  task.id = id;
  task.content = content;
  // ISO dates compare correctly as plain strings, so no date parsing is needed.
  task.overdue = collector->todayIso[0] != '\0' && dueDate[0] != '\0' && strcmp(dueDate, collector->todayIso) < 0;
  collector->out->push_back(std::move(task));
}
}  // namespace

TodoistClient::Error TodoistClient::fetchTodayTasks(const std::string& todayIso, std::vector<TodoistTask>& outTasks) {
  lastHttpCode = 0;
  if (!TODOIST_STORE.hasToken()) {
    LOG_DBG("TDA", "No API token configured");
    return NO_TOKEN;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  outTasks.clear();
  outTasks.reserve(TODOIST_MAX_TASKS);
  TaskCollector collector{&outTasks, todayIso.c_str()};
  TodoistTasksParser parser(collectTask, &collector);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(TODAY_FILTER_URL)) {
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
