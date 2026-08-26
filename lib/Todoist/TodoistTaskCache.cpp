#include "TodoistTaskCache.h"

#include <Logging.h>

#include <algorithm>

void TodoistTaskCache::toJson(JsonDocument& doc) const {
  doc["syncDate"] = syncDate;
  JsonArray arr = doc["tasks"].to<JsonArray>();
  for (const auto& task : tasks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = task.id;
    obj["content"] = task.content;
    // Stored as ISO so the file stays readable; overdue is derived on load and
    // deliberately not written, so the two can never disagree.
    char iso[11];
    todoist::isoFromDueDays(task.dueDays, iso, sizeof(iso));
    if (iso[0] != '\0') obj["due"] = iso;
  }
  JsonArray pending = doc["pending"].to<JsonArray>();
  for (const auto& id : pendingIds) {
    pending.add(id);
  }
  doc["completedToday"] = completedToday;
  doc["completedDay"] = completedDay;
}

bool TodoistTaskCache::fromJson(JsonVariantConst doc) {
  tasks.clear();
  pendingIds.clear();
  syncDate = doc["syncDate"] | "";
  completedToday = doc["completedToday"] | static_cast<uint16_t>(0);
  completedDay = doc["completedDay"] | todoist::DUE_NONE;

  JsonArrayConst arr = doc["tasks"].as<JsonArrayConst>();
  tasks.reserve(std::min(arr.size(), MAX_TASKS));
  for (JsonObjectConst obj : arr) {
    if (tasks.size() >= MAX_TASKS) break;
    TodoistTask task;
    task.id = obj["id"] | "";
    task.content = obj["content"] | "";
    task.dueDays = todoist::dueDaysFromIso(obj["due"] | "");
    if (task.id.empty()) continue;
    tasks.push_back(std::move(task));
  }
  // Flags come from syncDate, not the file. A cache written by the pre-"due"
  // build has no dates, so every task reads as undated until the next sync -
  // the list still renders, it just shows no overdue marks.
  applyOverdueFlags();

  JsonArrayConst pending = doc["pending"].as<JsonArrayConst>();
  pendingIds.reserve(std::min(pending.size(), MAX_PENDING));
  for (JsonVariantConst value : pending) {
    if (pendingIds.size() >= MAX_PENDING) break;
    const char* id = value | "";
    if (id[0] == '\0') continue;
    pendingIds.emplace_back(id);
  }

  LOG_DBG("TDC", "Loaded %zu tasks, %zu pending completions", tasks.size(), pendingIds.size());
  return true;
}

size_t TodoistTaskCache::getOverdueCount() const {
  return static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(), [](const TodoistTask& t) { return t.overdue; }));
}

void TodoistTaskCache::setTasks(std::vector<TodoistTask>&& fetched, const std::string& date) {
  tasks = std::move(fetched);
  if (tasks.size() > MAX_TASKS) tasks.resize(MAX_TASKS);
  // Ascending by due date: oldest overdue first, today's tasks last, undated
  // after those (DUE_NONE is the maximum). Stable, so the server's ordering
  // survives within a date.
  std::stable_sort(tasks.begin(), tasks.end(),
                   [](const TodoistTask& a, const TodoistTask& b) { return a.dueDays < b.dueDays; });
  // An empty date means today could not be established this sync; the header
  // keeps showing the last date it did know rather than falling back to "--".
  if (!date.empty()) syncDate = date;
  applyOverdueFlags();
  rolloverCompletedIfNeeded();
}

void TodoistTaskCache::applyOverdueFlags() {
  const uint16_t threshold = todoist::dueDaysFromIso(syncDate.c_str());
  for (auto& task : tasks) {
    // Undated tasks are never overdue, and nothing is flagged until today is
    // known - guessing would report a wrong count, which is worse than zero.
    task.overdue = threshold != todoist::DUE_NONE && task.dueDays != todoist::DUE_NONE && task.dueDays < threshold;
  }
}

void TodoistTaskCache::completeTaskAt(const size_t index) {
  if (index >= tasks.size()) return;
  if (pendingIds.size() < MAX_PENDING) {
    pendingIds.push_back(tasks[index].id);
  } else {
    LOG_ERR("TDC", "Pending completion queue full (%zu), dropping push for %s", MAX_PENDING, tasks[index].id.c_str());
  }
  tasks.erase(tasks.begin() + static_cast<long>(index));

  rolloverCompletedIfNeeded();
  if (completedToday < UINT16_MAX) completedToday++;
}

void TodoistTaskCache::setCompletedToday(const uint16_t count, const std::string& date) {
  if (!date.empty()) syncDate = date;
  completedToday = count;
  completedDay = todoist::dueDaysFromIso(syncDate.c_str());
}

void TodoistTaskCache::clearPending(const std::string& id) {
  pendingIds.erase(std::remove(pendingIds.begin(), pendingIds.end(), id), pendingIds.end());
}

void TodoistTaskCache::rolloverCompletedIfNeeded() {
  const uint16_t today = todoist::dueDaysFromIso(syncDate.c_str());
  // Undated ("today" unknown) leaves the counter alone rather than resetting
  // it against a sentinel: the same tolerance applyOverdueFlags() has for not
  // yet knowing what today is.
  if (today == todoist::DUE_NONE || completedDay == today) return;
  completedDay = today;
  completedToday = 0;
}
