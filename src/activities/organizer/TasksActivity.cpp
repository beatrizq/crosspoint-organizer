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
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"
#include "util/SleepWallpaperBackup.h"
#include "util/TaskWatchdog.h"

namespace {
// The sleep screen SleepActivity renders in CUSTOM mode, and the file the
// image viewer's "Set Cover" action writes.
constexpr char SLEEP_SCREEN_PATH[] = "/sleep.bmp";

// SNTP poll: 100ms x 50 = 5s, matching HalClock::syncFromNTP().
constexpr int NTP_POLL_ATTEMPTS = 50;

// Later of two "YYYY-MM-DD" dates. Today only ever moves forward, so picking the
// newest of the available sources is what keeps a partial one from regressing.
// ISO dates order correctly as plain strings, and "" loses to any real date.
const std::string& laterDate(const std::string& a, const std::string& b) { return b > a ? b : a; }
}  // namespace

void TasksActivity::loadCaches() {
  TODOIST_TASKS.loadFromFile();
  TODOIST_STORE.loadFromFile();
  rebuildTabs();
}

const char* TasksActivity::screenTitle() const { return tr(STR_ORGANIZER_TAB_TASKS); }

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
  char count[48];
  snprintf(count, sizeof(count), "%s: %d", tabLabel(tab()), rowCount());
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

const char* TasksActivity::rowConfirmLabel() const { return tr(STR_COMPLETE_TASK); }

void TasksActivity::onRowConfirm() { completeSelectedTask(); }

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
        // The popup answered on the press; this screen acts on the release, and
        // the button may still be down.
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
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

  LOG_DBG("TASKS", "Completing task: %s", TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].content.c_str());
  {
    // The render task reads the task list; hold the lock across the removal so
    // it never paints a half-updated list.
    RenderLock lock(*this);
    // Queued locally and pushed on the next sync, so completing works with the
    // radio off; the row leaves the list immediately either way.
    TODOIST_TASKS.completeTaskAt(static_cast<size_t>(cacheIndex));
    // Completing the last task in a tab takes that tab away, so the bar is rebuilt
    // before the selection is settled. rebuildTabs() keeps the same kind selected
    // where it survives and clamps the row selection itself.
    rebuildTabs();
    const int remaining = rowCount();
    if (selectedRow() >= remaining) selectedIndex = remaining;
    if (selectedIndex < 1) selectedIndex = remaining > 0 ? 1 : 0;
  }
  TODOIST_TASKS.saveToFile();
  // Wait for the repaint rather than firing and forgetting, so the framebuffer
  // holds the list without the completed task before it is snapshotted. The
  // sleep screen tracks the list, not the sync: a task completed with the radio
  // off changes what is on screen just as much as a fetch does.
  requestUpdateAndWait();
  saveSleepWallpaper();
}

// -- sync -------------------------------------------------------------------

void TasksActivity::startSync() {
  if (!TODOIST_STORE.hasToken()) {
    failSync(tr(STR_TODOIST_NO_TOKEN));
    return;
  }
  runSync([this] { performTaskSync(); });
}

bool TasksActivity::resolveTodayDate(std::string& outDate) const {
  // Most boards (X3/X4 included) have no RTC. This is the fallback source for
  // today: the fetch derives it from the response's newest due date, which is
  // both more reliable (it cannot fail when the fetch succeeded) and correct for
  // the account's timezone rather than this device's configured offset. NTP is
  // still needed for the cases the response cannot cover - an empty list, or one
  // containing nothing due today.
  outDate.clear();
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < NTP_POLL_ATTEMPTS && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(100);
    resetTaskWatchdogIfSubscribed();
  }
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    // Not an error on its own; the response usually supplies the date instead.
    LOG_DBG("TASKS", "NTP sync timed out; relying on the response date");
    return false;
  }

  // Boards that do have an RTC get it set from the same SNTP result.
  if (halClock.isAvailable()) halClock.syncFromNTP();

  uint8_t offsetQ = SETTINGS.clockUtcOffsetQ;
  if (offsetQ > 104) offsetQ = 104;  // clamp a corrupt persisted value to UTC+14
  const time_t local = time(nullptr) + (static_cast<int>(offsetQ) - 48) * 15 * 60;
  struct tm timeinfo;
  gmtime_r(&local, &timeinfo);

  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  outDate = buf;
  return true;
}

void TasksActivity::performTaskSync() {
  // Fallback source, tried first only because it needs the radio while it is
  // still up. The response date below wins when both are available.
  std::string ntpDate;
  if (!resolveTodayDate(ntpDate)) ntpDate.clear();

  // Push queued completions before fetching, so the fetched list already
  // reflects them. A copy: clearPending() mutates the queue as we go.
  const std::vector<std::string> pending = TODOIST_TASKS.getPendingIds();
  TodoistClient::Error error = TodoistClient::OK;
  for (const auto& id : pending) {
    // Each push is a full TLS request; the sync runs on the main task.
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::closeTask(id);
    if (error != TodoistClient::OK) {
      LOG_ERR("TASKS", "Push failed for %s: %s", id.c_str(), TodoistClient::errorString(error));
      break;  // keep the rest queued for the next attempt
    }
    TODOIST_TASKS.clearPending(id);
  }

  std::vector<TodoistTask> fetched;
  std::string serverDate;
  if (error == TodoistClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::fetchTasks(fetched, serverDate);
    resetTaskWatchdogIfSubscribed();
  }

  // NTP first when it worked: it is the only source that knows the configured UTC
  // offset, so it gives the device's own local date. The response's Date header
  // is the fallback - it cannot be blocked the way NTP can, but it is GMT, so it
  // can read a day off either side of midnight. The previous sync then keeps the
  // date from going backwards if both are unavailable or disagree downwards,
  // because today never moves back.
  std::string today = ntpDate.empty() ? serverDate : ntpDate;
  today = laterDate(today, TODOIST_TASKS.getSyncDate());
  if (today.empty()) {
    LOG_ERR("TASKS", "Today unresolved: no NTP, no Date header, no previous sync");
  }

  // Drop the radio before touching the SD card and repainting; the full
  // teardown happens on the silent reboot in onExit().
  tearDownRadio();

  if (error == TodoistClient::OK) {
    RenderLock lock(*this);
    TODOIST_TASKS.setTasks(std::move(fetched), today);
    // A new result set means new row counts, so which tabs exist changes with it.
    rebuildTabs();
  }
  // finishSync settles the state and repaints; the list is already in place.
  finishSync(error == TodoistClient::OK ? nullptr : taskErrorText(error));

  // Persists the fetched list and whatever the queue push managed to clear.
  TODOIST_TASKS.saveToFile();

  if (error != TodoistClient::OK) return;
  // Wait for the repaint so the framebuffer holds the new list, then snapshot
  // it as the sleep screen.
  requestUpdateAndWait();
  saveSleepWallpaper();
}

void TasksActivity::saveSleepWallpaper() const {
  if (!TODOIST_STORE.getSleepScreenEnabled()) {
    // Logged so a sync that leaves the sleep screen untouched is distinguishable
    // from one where this was never reached.
    LOG_DBG("TASKS", "Sleep screen disabled in settings; wallpaper not updated");
    return;
  }

  const uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!framebuffer) {
    LOG_ERR("TASKS", "Framebuffer unavailable; sleep screen not updated");
    return;
  }
  // Whatever wallpaper is there is the user's until this screen replaces it, so
  // it is copied aside first - once, on the first replacement - and handed back
  // when the option is switched off. Without it, turning the option on destroyed
  // a chosen wallpaper and turning it off left the task list in its place.
  SleepWallpaperBackup::captureIfAbsent();

  // Same file and format the "Set Cover" action writes from the image viewer,
  // so SleepActivity's CUSTOM mode picks it up unchanged.
  if (!ScreenshotUtil::saveFramebufferAsBmp(SLEEP_SCREEN_PATH, framebuffer, renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR("TASKS", "Failed to write %s", SLEEP_SCREEN_PATH);
    return;
  }
  LOG_DBG("TASKS", "Sleep screen updated from the task list");

  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    // Remembered before it is overwritten, and only the first time: a later sync
    // would otherwise record CUSTOM as the mode to go back to.
    if (TODOIST_STORE.getPreviousSleepScreen() == TodoistStore::NO_SLEEP_SCREEN) {
      TODOIST_STORE.setPreviousSleepScreen(SETTINGS.sleepScreen);
      TODOIST_STORE.saveToFile();
    }
    // The wallpaper is only shown in CUSTOM mode; switching is what makes the
    // snapshot visible at all.
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    LOG_INF("TASKS", "Sleep screen mode switched to custom");
  }
}

const char* TasksActivity::taskErrorText(const TodoistClient::Error error) {
  switch (error) {
    case TodoistClient::NO_TOKEN:
      return tr(STR_TODOIST_NO_TOKEN);
    case TodoistClient::AUTH_FAILED:
      return tr(STR_TODOIST_INVALID_TOKEN);
    case TodoistClient::SERVER_ERROR:
      return tr(STR_TODOIST_SERVER_ERROR);
    case TodoistClient::PARSE_ERROR:
      return tr(STR_TODOIST_BAD_RESPONSE);
    case TodoistClient::INVALID_FILTER:
      return tr(STR_TODOIST_INVALID_FILTER);
    case TodoistClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}
