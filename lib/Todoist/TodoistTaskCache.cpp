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
    obj["overdue"] = task.overdue;
  }
  JsonArray pending = doc["pending"].to<JsonArray>();
  for (const auto& id : pendingIds) {
    pending.add(id);
  }
}

bool TodoistTaskCache::fromJson(JsonVariantConst doc) {
  tasks.clear();
  pendingIds.clear();
  syncDate = doc["syncDate"] | "";

  JsonArrayConst arr = doc["tasks"].as<JsonArrayConst>();
  tasks.reserve(std::min(arr.size(), MAX_TASKS));
  for (JsonObjectConst obj : arr) {
    if (tasks.size() >= MAX_TASKS) break;
    TodoistTask task;
    task.id = obj["id"] | "";
    task.content = obj["content"] | "";
    task.overdue = obj["overdue"] | false;
    if (task.id.empty()) continue;
    tasks.push_back(std::move(task));
  }

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
  // Overdue first; stable so the server's ordering survives within each group.
  std::stable_partition(tasks.begin(), tasks.end(), [](const TodoistTask& t) { return t.overdue; });
  // An empty date means the clock could not be resolved this sync; the header
  // keeps showing the last date it did know rather than falling back to "--".
  if (!date.empty()) syncDate = date;
}

void TodoistTaskCache::completeTaskAt(const size_t index) {
  if (index >= tasks.size()) return;
  if (pendingIds.size() < MAX_PENDING) {
    pendingIds.push_back(tasks[index].id);
  } else {
    LOG_ERR("TDC", "Pending completion queue full (%zu), dropping push for %s", MAX_PENDING, tasks[index].id.c_str());
  }
  tasks.erase(tasks.begin() + static_cast<long>(index));
}

void TodoistTaskCache::clearPending(const std::string& id) {
  pendingIds.erase(std::remove(pendingIds.begin(), pendingIds.end(), id), pendingIds.end());
}
