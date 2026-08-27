#pragma once
#include <TodoistClient.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "OrganizerScreenActivity.h"

/**
 * The Tasks screen: whatever the Todoist Filter setting matches, split by date.
 *
 * What arrives is the user's business, not this screen's: the Filter setting
 * holds a Todoist filter query and the sync asks for exactly that. The tabs only
 * partition the result - they no longer decide what it is, which is what the
 * hardcoded "overdue | due before: +30 days" used to do.
 *
 * All leads with the whole result. Overdue, Today and Upcoming then split the
 * dated tasks around the date the last sync settled on - before it, on it, after
 * it - and No date collects the rest. A purely date-based filter never returns
 * undated tasks, so No date is empty by construction for one; a filter like
 * "view all" fills all five.
 *
 * The tab bar is built from what is actually there: a tab with no rows is left
 * out entirely, so an inbox with nothing overdue does not carry a dead Overdue
 * tab. All is the exception and always shows, being the one tab a successful sync
 * cannot leave empty. That makes the tab set change under the user - after a sync,
 * and after completing the last task in a tab - so it is rebuilt on both, holding
 * the same *kind* of tab selected rather than the same index.
 *
 * The three dated tabs are all empty until a sync establishes today: without it
 * there is no before, on or after, and filing a task under a guessed date is
 * worse than showing it only under All.
 */
class TasksActivity final : public OrganizerScreenActivity {
 public:
  // What a tab holds. Not a tab index: which of these are on screen depends on
  // what the filter returned, so the two are mapped through `visibleTabs`.
  enum class TabKind : uint8_t { ALL, OVERDUE, TODAY, UPCOMING, NO_DATE };

  // selectTaskId, when non-empty, jumps straight to that task on first paint:
  // whichever tab it falls in, at its own row, rather than wherever
  // initialTab/row 0 would otherwise land. One-shot -- cleared once applied,
  // so a later tab switch behaves normally.
  explicit TasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int initialTab = 0,
                         std::string selectTaskId = "")
      : OrganizerScreenActivity("Tasks", renderer, mappedInput, initialTab), selectTaskId(std::move(selectTaskId)) {}

  void onEnter() override;

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return static_cast<int>(visibleTabs.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // The due date goes on a second line wherever it tells two rows apart. Not on
  // Today, where every row is due today and the line would repeat the tab's own
  // name, and not on No date, where there is no date to draw.
  bool rowsHaveSubtitle() const override;
  const char* rowConfirmLabel() const override;
  void onRowConfirm() override;
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::TASKS; }
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Tasks; }

 private:
  // The kind on screen at `index`, or ALL when the index is out of range.
  TabKind kindAt(int index) const;
  // The kind the active tab holds.
  TabKind currentKind() const { return kindAt(tab()); }

  // Whether a task belongs to `kind`. ALL takes everything; the rest split the
  // list around today, with No date taking the undated.
  bool matchesKind(TabKind kind, size_t cacheIndex) const;
  int countFor(TabKind kind) const;

  // Recomputes which tabs have rows, keeping the active *kind* selected where it
  // survives and falling back to All where it does not. Also clamps the selection
  // into the new tab's row count, since a rebuild can shorten the list under it.
  void rebuildTabs();

  // The cache index behind a visible row, or -1. Scanned rather than cached in a
  // vector: the list is capped at TODOIST_MAX_TASKS, and a stored mapping would
  // have to be rebuilt on every sync, tab switch and completion.
  int cacheIndexForRow(int row) const;

  // Select opens this first, rather than completeSelectedTask() directly --
  // see rowConfirmLabel()/onRowConfirm().
  void showRowOptions();
  // Asks first; performTaskCompletion() is what actually closes the task.
  void completeSelectedTask();
  void performTaskCompletion(int cacheIndex);
  void performTaskSync();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the same task.
  void offerFocusSession(int cacheIndex);
  // The Options menu's "Reschedule" entry opens this: a warning first if the
  // task is recurring (see TodoistTask::isRecurring), then the date picker.
  void offerReschedule(int cacheIndex);

  // Tabs currently on screen, in display order. Always leads with ALL.
  std::vector<TabKind> visibleTabs{TabKind::ALL};

  // See the constructor comment. Consumed and cleared in onEnter().
  std::string selectTaskId;
};
