#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "TodoistTask.h"

// One task's due date changed on the device, awaiting push to the server.
struct TodoistPendingReschedule {
  std::string taskId;
  uint16_t dueDays = todoist::DUE_NONE;
};

/**
 * Singleton holding the last synced task list plus the completions and
 * reschedules that have not reached the server yet.
 *
 * The Today screen renders straight from here, so opening it needs no Wi-Fi:
 * a sync is an explicit user action (hold Select), and completing or
 * rescheduling a task only updates this cache. The queued changes are pushed
 * on the next sync, before the list is re-fetched, so the fresh list already
 * reflects them.
 */
class TodoistTaskCache : public PersistableStore<TodoistTaskCache> {
 private:
  std::vector<TodoistTask> tasks;       // Overdue first, then due today
  std::vector<std::string> pendingIds;  // Completed locally, awaiting push
  // Rescheduled locally, awaiting push. Keyed by task id rather than cache
  // index, since a task's index shifts whenever another task ahead of it is
  // completed or the list is re-sorted after a sync.
  std::vector<TodoistPendingReschedule> pendingReschedules;
  std::string syncDate;         // Local date of the last sync, "YYYY-MM-DD"
  uint16_t completedToday = 0;  // Tasks completed on this device today
  // Day completedToday belongs to, keyed the same way syncDate's day is
  // (see completeTaskAt): the class already treats syncDate as "today"
  // everywhere else, so the completion counter follows the same convention
  // rather than introducing a second notion of today.
  uint16_t completedDay = todoist::DUE_NONE;

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
  // Overdue plus due exactly today, per syncDate's notion of "today" - the
  // Home screen's notification-style badge for this app.
  size_t getDueTodayOrOverdueCount() const;
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

  // Updates the task's due date immediately, for instant feedback, and queues
  // the change for push on the next sync - the same offline-first pattern
  // completeTaskAt() already uses. Rescheduling the same task again before a
  // sync replaces the queued date rather than adding a second entry. No-op
  // for an unknown index.
  void rescheduleTaskAt(size_t index, uint16_t newDueDays);

  const std::vector<TodoistPendingReschedule>& getPendingReschedules() const { return pendingReschedules; }
  // Called once the server accepted a queued reschedule, or the task it was
  // for turned out to already be gone.
  void clearPendingReschedule(const std::string& id);

  // Tasks completed today, per syncDate's notion of "today" - on this device,
  // or anywhere else once a sync has confirmed it (see setCompletedToday).
  // Stale until the next sync if the day rolled over with no completion yet
  // to trigger the rollover - the same staleness every other syncDate-derived
  // figure in this class already tolerates between syncs.
  uint16_t getCompletedToday() const { return completedToday; }

  // Sets today's completed count directly, from a fetch that already reflects
  // the whole day: this device's own presses once pushed, and anything
  // finished in the Todoist app or on the web. Replaces rather than adds -
  // the fetch is authoritative for the day, not incremental - and marks
  // completedDay resolved so a completion pressed on-device later the same
  // day still adds on top of this baseline instead of rolling over first.
  void setCompletedToday(uint16_t count, const std::string& date);

 private:
  // Recomputes every task's overdue flag against syncDate. The flag is derived
  // state, so it is set here rather than stored by the parser or the file.
  void applyOverdueFlags();

  // Zeroes completedToday the first time syncDate's day moves past
  // completedDay. Shared by setTasks (a sync can itself roll the day over)
  // and completeTaskAt (a local completion can too, between syncs).
  void rolloverCompletedIfNeeded();
};

#define TODOIST_TASKS TodoistTaskCache::getInstance()
