#pragma once
#include <TodoistClient.h>

#include <cstddef>
#include <string>

#include "OrganizerScreenActivity.h"

/**
 * The Tasks screen: the synced Todoist list, split across its own tab bar.
 *
 * Tasks was one tab of the Organizer screen. It is now a screen in its own
 * right, and the tab bar it inherited is used for what the list is actually made
 * of - All, Overdue, Today, Upcoming - rather than for switching to the calendar.
 *
 * All leads: it is the whole synced list, and the one tab that cannot be empty
 * while anything is synced. Overdue, Today and Upcoming then split it against
 * the date the last sync settled on - before it, on it, after it. The fetch asks
 * for everything overdue plus the next TODOIST_WINDOW_DAYS days, so all three
 * have something to show.
 *
 * The three dated tabs are all empty until a sync establishes today: without it
 * there is no before, on or after, and filing a task under a guessed date is
 * worse than showing it only under All.
 *
 * Completing a task is queued locally and pushed on the next sync, so it works
 * with the radio off.
 */
class TasksActivity final : public OrganizerScreenActivity {
 public:
  enum class Tab : uint8_t { ALL = 0, OVERDUE = 1, TODAY = 2, UPCOMING = 3 };
  static constexpr int TAB_COUNT = 4;

  explicit TasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Tab initialTab = Tab::ALL)
      : OrganizerScreenActivity("Tasks", renderer, mappedInput, static_cast<int>(initialTab)) {}

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return TAB_COUNT; }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  const char* rowConfirmLabel() const override;
  void onRowConfirm() override;
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::TASKS; }

 private:
  // Whether a task belongs in the tab being shown. All takes everything; the
  // other three split the list around today.
  bool matchesTab(size_t cacheIndex) const;
  // The cache index behind a visible row, or -1. Scanned rather than cached in a
  // vector: the list is capped at TODOIST_MAX_TASKS, and a stored mapping would
  // have to be rebuilt on every sync, tab switch and completion.
  int cacheIndexForRow(int row) const;

  // Asks first; performTaskCompletion() is what actually closes the task.
  void completeSelectedTask();
  void performTaskCompletion(int cacheIndex);
  void performTaskSync();
  bool resolveTodayDate(std::string& outDate) const;
  void saveSleepWallpaper() const;
  static const char* taskErrorText(TodoistClient::Error error);
};
