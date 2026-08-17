#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "TodoistTask.h"

/**
 * Singleton holding the last synced task list plus the completions that have
 * not reached the server yet.
 *
 * The Today screen renders straight from here, so opening it needs no Wi-Fi:
 * a sync is an explicit user action (hold Select), and completing a task only
 * updates this cache. The queued ids are pushed on the next sync, before the
 * list is re-fetched, so the fresh list already reflects them.
 */
class TodoistTaskCache : public PersistableStore<TodoistTaskCache> {
 private:
  std::vector<TodoistTask> tasks;       // Overdue first, then due today
  std::vector<std::string> pendingIds;  // Completed locally, awaiting push
  std::string syncDate;                 // Local date of the last sync, "YYYY-MM-DD"

  TodoistTaskCache() = default;
  ~TodoistTaskCache() = default;

  friend class PersistableStore<TodoistTaskCache>;

 public:
  static constexpr size_t MAX_TASKS = TODOIST_MAX_TASKS;
  static constexpr size_t MAX_PENDING = TODOIST_MAX_TASKS;

  static const char* getFilePath() { return "/.crosspoint/todoist_tasks.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<TodoistTask>& getTasks() const { return tasks; }
  size_t getOverdueCount() const;
  const std::string& getSyncDate() const { return syncDate; }
  bool hasSynced() const { return !syncDate.empty(); }

  // Replace the list after a successful fetch.
  //
  // Sorted by due date ascending, so the oldest overdue task leads and tasks
  // due today trail; undated tasks (DUE_NONE) sort last. Stable, so the
  // server's ordering survives within a single date.
  //
  // `date` is today as "YYYY-MM-DD" and sets the overdue threshold: anything
  // due strictly before it is flagged. An empty date leaves the stored date
  // untouched and clears no flags, so a sync that could not establish today
  // keeps showing the last date it did know.
  void setTasks(std::vector<TodoistTask>&& fetched, const std::string& date);

  // Drop the task locally and remember to close it on the server. No-op for an
  // unknown index.
  void completeTaskAt(size_t index);

  const std::vector<std::string>& getPendingIds() const { return pendingIds; }
  bool hasPending() const { return !pendingIds.empty(); }
  // Called once the server accepted (or already knew about) the completion.
  void clearPending(const std::string& id);

 private:
  // Recomputes every task's overdue flag against syncDate. The flag is derived
  // state, so it is set here rather than stored by the parser or the file.
  void applyOverdueFlags();
};

#define TODOIST_TASKS TodoistTaskCache::getInstance()
