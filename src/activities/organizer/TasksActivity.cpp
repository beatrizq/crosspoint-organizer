#include "TasksActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <esp_sntp.h>
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OrganizerLabels.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/OrganizerActions.h"
#include "util/OrganizerSync.h"
#include "util/TaskWatchdog.h"

void TasksActivity::loadCaches() {
  TODOIST_TASKS.loadFromFile();
  TODOIST_STORE.loadFromFile();
  rebuildTabs();
}

void TasksActivity::onEnter() {
  OrganizerScreenActivity::onEnter();
  if (selectTaskId.empty()) return;

  const std::string targetId = std::move(selectTaskId);
  selectTaskId.clear();  // one-shot, regardless of whether the id is still found below

  const auto& tasks = TODOIST_TASKS.getTasks();
  int targetCacheIndex = -1;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == targetId) {
      targetCacheIndex = static_cast<int>(i);
      break;
    }
  }
  // Gone (completed/deleted since the pick was made): leave onEnter()'s
  // default selection (tab 0, row 0) rather than landing on nothing.
  if (targetCacheIndex < 0) return;

  // ALL (always visibleTabs[0]) matches everything, so it is only used as the
  // fallback -- a more specific tab, when one matches, is what the task
  // actually belongs to.
  int targetTab = 0;
  for (size_t t = 1; t < visibleTabs.size(); t++) {
    if (matchesKind(visibleTabs[t], static_cast<size_t>(targetCacheIndex))) {
      targetTab = static_cast<int>(t);
      break;
    }
  }
  setTab(targetTab);

  const int rows = rowCount();
  for (int row = 0; row < rows; row++) {
    if (cacheIndexForRow(row) == targetCacheIndex) {
      selectedIndex = row + 1;
      break;
    }
  }
}

const char* TasksActivity::screenTitle() const { return homeAppOrder::displayName(homeAppOrder::AppId::Tasks); }

TasksActivity::TabKind TasksActivity::kindAt(const int index) const {
  if (index < 0 || static_cast<size_t>(index) >= visibleTabs.size()) return TabKind::ALL;
  return visibleTabs[static_cast<size_t>(index)];
}

const char* TasksActivity::tabLabel(const int index) const {
  switch (kindAt(index)) {
    case TabKind::ALL:
      return tr(STR_TASKS_TAB_ALL);
    case TabKind::OVERDUE:
      return tr(STR_OVERDUE);
    case TabKind::TODAY:
      return tr(STR_TODAY);
    case TabKind::UPCOMING:
      return tr(STR_TASKS_TAB_UPCOMING);
    case TabKind::NO_DATE:
      return tr(STR_TASKS_TAB_NO_DATE);
  }
  return "";
}

void TasksActivity::rebuildTabs() {
  // The kind selected now, so the same tab stays under the user across a rebuild
  // even though its index may move when a tab ahead of it appears or goes.
  const TabKind wanted = currentKind();

  visibleTabs.clear();
  visibleTabs.reserve(5);
  // All always shows: it is the whole filter result, and the one tab a successful
  // sync cannot leave empty. The rest earn their place by having rows, so an
  // inbox with nothing overdue carries no dead Overdue tab.
  visibleTabs.push_back(TabKind::ALL);
  for (const TabKind kind : {TabKind::OVERDUE, TabKind::TODAY, TabKind::UPCOMING, TabKind::NO_DATE}) {
    if (countFor(kind) > 0) visibleTabs.push_back(kind);
  }

  int restored = 0;
  for (size_t i = 0; i < visibleTabs.size(); i++) {
    if (visibleTabs[i] == wanted) {
      restored = static_cast<int>(i);
      break;
    }
  }
  // Falls back to All when the selected kind just emptied - completing the last
  // overdue task, say, which takes its tab away while the user is standing on it.
  setTab(restored);

  // The new tab's list can be shorter than the old one, so the row selection has
  // to be pulled back inside it. Index 0 is the tab bar, which is always valid.
  const int rows = rowCount();
  if (selectedRow() >= rows) selectedIndex = rows;
  if (selectedIndex < 0) selectedIndex = 0;
}

// -- rows -------------------------------------------------------------------

bool TasksActivity::matchesKind(const TabKind kind, const size_t cacheIndex) const {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return false;
  const TodoistTask& task = tasks[cacheIndex];

  // Today, as the last sync settled it. DUE_NONE when nothing has synced or the
  // date could not be established, which is why the three dated tabs check it:
  // without today there is no before, on, or after to sort a task into, and
  // guessing would file it under the wrong one.
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  const bool dated = task.dueDays != todoist::DUE_NONE;
  const bool knowToday = today != todoist::DUE_NONE;

  switch (kind) {
    case TabKind::ALL:
      return true;
    case TabKind::OVERDUE:
      // The cache owns this flag, against the same date, so the tab agrees with
      // the overdue count in the header by construction.
      return task.overdue;
    case TabKind::TODAY:
      return knowToday && dated && task.dueDays == today;
    case TabKind::UPCOMING:
      // Strictly after today, and dated: DUE_NONE is the maximum, so an undated
      // task would otherwise read as the furthest-future one there is.
      return knowToday && dated && task.dueDays > today;
    case TabKind::NO_DATE:
      // No date needs no date: this is the one tab that means something before a
      // sync has worked out what today is.
      return !dated;
  }
  return false;
}

int TasksActivity::countFor(const TabKind kind) const {
  const auto& tasks = TODOIST_TASKS.getTasks();
  int count = 0;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (matchesKind(kind, i)) count++;
  }
  return count;
}

int TasksActivity::rowCount() const { return countFor(currentKind()); }

int TasksActivity::cacheIndexForRow(const int row) const {
  if (row < 0) return -1;
  const auto& tasks = TODOIST_TASKS.getTasks();
  const TabKind kind = currentKind();
  int seen = 0;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (!matchesKind(kind, i)) continue;
    if (seen == row) return static_cast<int>(i);
    seen++;
  }
  return -1;
}

void TasksActivity::drawRow(const RowLayout& layout) const {
  const int cacheIndex = cacheIndexForRow(layout.index);
  if (cacheIndex < 0) return;
  const auto& task = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)];
  const auto shown = renderer.truncatedText(layout.titleFont, task.content.c_str(), layout.width);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shown.c_str(), layout.ink);

  if (!rowsHaveSubtitle()) return;
  // The due date under the task, as the Calendar screen dates an event - same
  // format, because a date reads the same wherever it appears on these screens.
  // Undated tasks draw nothing rather than "--": the row keeps its height, so the
  // list stays even, and an empty line says "no date" more quietly than a dash.
  if (task.dueDays == todoist::DUE_NONE) return;
  char when[16];
  organizer::formatDayLabel(task.dueDays, when, sizeof(when));
  const auto shownWhen = renderer.truncatedText(layout.subtitleFont, when, layout.width);
  const int whenY = layout.textY + renderer.getLineHeight(layout.titleFont);
  renderer.drawText(layout.subtitleFont, layout.x, whenY, shownWhen.c_str(), layout.ink);
  // Greyed, so the date stays subordinate to the task rather than competing with
  // it. The smaller font alone was not enough separation.
  dimText(layout.x, whenY, layout.subtitleFont, shownWhen.c_str(), layout.ink);
}

void TasksActivity::formatStatus(char* out, const size_t outSize) const {
  char date[16];
  organizer::formatDayLabel(civil::dateFromIso(TODOIST_TASKS.getSyncDate().c_str()), date, sizeof(date));
  char count[32];
  snprintf(count, sizeof(count), tr(STR_TODOIST_DONE_TODAY), static_cast<int>(TODOIST_TASKS.getCompletedToday()));
  if (TODOIST_TASKS.hasPending()) {
    char waiting[32];
    snprintf(waiting, sizeof(waiting), tr(STR_TODOIST_PENDING_COMPLETIONS),
             static_cast<int>(TODOIST_TASKS.getPendingIds().size()));
    snprintf(out, outSize, "%s %s | %s", date, waiting, count);
    return;
  }
  snprintf(out, outSize, "%s  ·  %s", date, count);
}

const char* TasksActivity::emptyMessage() const {
  if (!TODOIST_TASKS.hasSynced()) return tr(STR_TODOIST_NEVER_SYNCED);
  // Reached on All, since every other tab is hidden when it has no rows. An empty
  // All after a successful sync means the filter matched nothing, which is a
  // different problem from having no tasks - and the one the user can act on.
  return TODOIST_TASKS.getTasks().empty() ? tr(STR_TODOIST_FILTER_NO_MATCH) : tr(STR_TODOIST_NO_TASKS);
}

const char* TasksActivity::syncingMessage() const { return tr(STR_TODOIST_SYNCING); }

// -- completion -------------------------------------------------------------

bool TasksActivity::rowsHaveSubtitle() const {
  const TabKind kind = currentKind();
  return kind != TabKind::TODAY && kind != TabKind::NO_DATE;
}

const char* TasksActivity::rowConfirmLabel() const { return tr(STR_SELECT); }

void TasksActivity::onRowConfirm() { showRowOptions(); }

void TasksActivity::showRowOptions() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;

  std::vector<std::string> options{tr(STR_COMPLETE_TASK), tr(STR_FOCUS_SESSION)};
  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the
        // press, this screen on the release) -- same dance completeSelectedTask()
        // does below for the popup it pushes in turn.
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        // idx 1 (Focus session) has nothing to do yet.
        if (std::get<OptionPickResult>(result.data).index == 0) completeSelectedTask();
      });
}

void TasksActivity::completeSelectedTask() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;

  // Asked rather than done: completing pushes to Todoist and cannot be undone
  // from the device, and Select is the same button that switches tabs one row
  // up. The prompt names the task, because the list is behind it by then, and
  // it opens on Cancel.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                             TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].content),
      [this, cacheIndex](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the press,
        // this screen on the release). Back is swallowed whenever the result was
        // cancelled at all, since dismissing the popup with Back can itself be
        // release-triggered - by then the button is no longer down, but the
        // release is still what this screen would see next.
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) {
          LOG_DBG("TASKS", "Task completion cancelled");
          return;
        }
        performTaskCompletion(cacheIndex);
      });
}

void TasksActivity::performTaskCompletion(const int cacheIndex) {
  // Re-checked: the prompt sat on top of this screen for as long as the user
  // took to answer, and an index is not a task.
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size()) return;

  {
    // The render task reads the task list; hold the lock across the removal so
    // it never paints a half-updated list.
    RenderLock lock(*this);
    organizerActions::completeTask(static_cast<size_t>(cacheIndex));
    // Completing the last task in a tab takes that tab away, so the bar is rebuilt
    // before the selection is settled. rebuildTabs() keeps the same kind selected
    // where it survives and clamps the row selection itself.
    rebuildTabs();
    const int remaining = rowCount();
    if (selectedRow() >= remaining) selectedIndex = remaining;
    if (selectedIndex < 1) selectedIndex = remaining > 0 ? 1 : 0;
  }
  // The sleep screen tracks the list, not the sync: a task completed with the
  // radio off changes what is on screen just as much as a fetch does.
  updateSleepScreen();
}

// -- sync -------------------------------------------------------------------

void TasksActivity::startSync() {
  if (!TODOIST_STORE.hasToken()) {
    failSync(tr(STR_TODOIST_NO_TOKEN));
    return;
  }
  runSync([this] { performTaskSync(); });
}

void TasksActivity::performTaskSync() {
  // The requests and the cache update live in organizerSync so the home screen's
  // sync-everything can drive the same sequence over one Wi-Fi association. What
  // stays here is what only this screen can do.
  const char* failure = organizerSync::run(organizerSync::Service::Tasks);

  // Drop the radio before repainting; the full teardown happens on the silent
  // reboot in onExit().
  tearDownRadio();

  if (failure == nullptr) {
    // A new result set means new row counts, so which tabs exist changes with it.
    RenderLock lock(*this);
    rebuildTabs();
  }
  finishSync(failure);

  if (failure != nullptr) return;
  // A new list is a change worth showing on a sleeping device. No-op unless this
  // app is the chosen sleep screen source; sync-everything cannot do this, since
  // its own progress list is what is on screen there.
  updateSleepScreen();
}
