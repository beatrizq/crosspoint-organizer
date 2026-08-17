#include "TodoistActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <time.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"
#include "util/TaskWatchdog.h"

namespace {
// The sleep screen SleepActivity renders in CUSTOM mode, and the file the
// image viewer's "Set Cover" action writes.
constexpr char SLEEP_SCREEN_PATH[] = "/sleep.bmp";

// Hold threshold for "sync now" on the Select button (firmware convention).
constexpr unsigned long LONG_PRESS_MS = 1000;

// SNTP poll: 100ms x 50 = 5s, matching HalClock::syncFromNTP().
constexpr int NTP_POLL_ATTEMPTS = 50;

// Reformats "YYYY-MM-DD" as "DD-MM-YYYY". Writes "--" when the date is unset or
// not the expected shape (a hand-edited cache file, or a never-synced device).
void formatDisplayDate(const std::string& iso, char* out, const size_t outSize) {
  if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') {
    snprintf(out, outSize, "--");
    return;
  }
  snprintf(out, outSize, "%.2s-%.2s-%.4s", iso.c_str() + 8, iso.c_str() + 5, iso.c_str());
}

// Later of two "YYYY-MM-DD" dates. Today only ever moves forward, so picking the
// newest of the available sources is what keeps a partial one from regressing.
// ISO dates order correctly as plain strings, and "" loses to any real date.
const std::string& laterDate(const std::string& a, const std::string& b) { return b > a ? b : a; }
}  // namespace

TodoistActivity::TodoistActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Todoist", renderer, mappedInput) {}

void TodoistActivity::onEnter() {
  Activity::onEnter();
  TODOIST_TASKS.loadFromFile();
  selectedIndex = 0;
  if (!TODOIST_STORE.hasToken()) {
    state = State::FAILED;
    statusMessage = tr(STR_TODOIST_NO_TOKEN);
  }
  requestUpdate();
}

void TodoistActivity::onExit() {
  Activity::onExit();

  // Same teardown as the KOReader sync screen: drop the association, then
  // reboot silently to home so the WiFi/TLS heap fragmentation goes with it.
  // The mode check keeps a cancelled Wi-Fi picker (radio never brought up)
  // from costing a reboot.
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

int TodoistActivity::taskCount() const { return static_cast<int>(TODOIST_TASKS.getTasks().size()); }

void TodoistActivity::completeSelected() {
  if (selectedIndex < 0 || selectedIndex >= taskCount()) return;

  LOG_DBG("TDA", "Completing task: %s", TODOIST_TASKS.getTasks()[selectedIndex].content.c_str());
  {
    // The render task reads the task list; hold the lock across the removal so
    // it never paints a half-updated list.
    RenderLock lock(*this);
    // Queued locally and pushed on the next sync, so completing works with the
    // radio off; the row leaves the list immediately either way.
    TODOIST_TASKS.completeTaskAt(static_cast<size_t>(selectedIndex));
    if (selectedIndex >= taskCount()) selectedIndex = taskCount() - 1;
    if (selectedIndex < 0) selectedIndex = 0;
  }
  TODOIST_TASKS.saveToFile();
  requestUpdate(true);
}

void TodoistActivity::loop() {
  if (state == State::SYNCING) return;  // ignore input while the sync blocks

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::FAILED) {
      // Dismiss the failure message and fall back to whatever is cached.
      {
        RenderLock lock(*this);
        state = State::LIST;
        statusMessage = nullptr;
      }
      requestUpdate(true);
    } else if (mappedInput.getHeldTime() >= LONG_PRESS_MS || taskCount() == 0) {
      // Hold syncs; with nothing to complete, a plain press syncs too.
      startSync();
    } else {
      completeSelected();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int itemCount = taskCount();
  if (state != State::LIST || itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      // The row action is "complete"; handleListTouch already moved the
      // highlight to the tapped row.
      completeSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void TodoistActivity::startSync() {
  if (!TODOIST_STORE.hasToken()) {
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_TODOIST_NO_TOKEN);
    }
    requestUpdate(true);
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  requestUpdate();

  // Past this point every path uses WiFi, so onExit() owes a teardown.
  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    performSync();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             {
                               RenderLock lock(*this);
                               state = State::FAILED;
                               statusMessage = tr(STR_WIFI_CONN_FAILED);
                             }
                             requestUpdate(true);
                             return;
                           }
                           performSync();
                         });
}

bool TodoistActivity::resolveTodayDate(std::string& outDate) const {
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
    LOG_DBG("TDA", "NTP sync timed out; relying on the response date");
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
  LOG_DBG("TDA", "Today resolved as %s", buf);
  return true;
}

void TodoistActivity::performSync() {
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
      LOG_ERR("TDA", "Push failed for %s: %s", id.c_str(), TodoistClient::errorString(error));
      break;  // keep the rest queued for the next attempt
    }
    TODOIST_TASKS.clearPending(id);
  }

  std::vector<TodoistTask> fetched;
  std::string derivedDate;
  if (error == TodoistClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::fetchTodayTasks(fetched, derivedDate);
    resetTaskWatchdogIfSubscribed();
  }

  // Today is the newest of every source we have. The response date is exact
  // whenever something is due today, but reads early for an all-overdue list, so
  // NTP and the previous sync guard against the date going backwards.
  std::string today = laterDate(derivedDate, ntpDate);
  today = laterDate(today, TODOIST_TASKS.getSyncDate());
  if (today.empty()) {
    LOG_ERR("TDA", "Today unresolved: no dated tasks, no NTP, no previous sync");
  } else {
    LOG_DBG("TDA", "Today resolved as %s (response '%s', ntp '%s')", today.c_str(), derivedDate.c_str(),
            ntpDate.c_str());
  }

  // Drop the radio before touching the SD card and repainting; the full
  // teardown happens on the silent reboot in onExit().
  esp_wifi_stop();

  {
    RenderLock lock(*this);
    if (error == TodoistClient::OK) {
      TODOIST_TASKS.setTasks(std::move(fetched), today);
      state = State::LIST;
      statusMessage = nullptr;
      selectedIndex = 0;
    } else {
      state = State::FAILED;
      statusMessage = errorText(error);
    }
  }
  // Persists the fetched list and whatever the queue push managed to clear.
  TODOIST_TASKS.saveToFile();

  if (error != TodoistClient::OK) {
    requestUpdate(true);
    return;
  }
  // Wait for the repaint so the framebuffer holds the new list, then snapshot
  // it as the sleep screen.
  requestUpdateAndWait();
  saveSleepWallpaper();
}

void TodoistActivity::saveSleepWallpaper() const {
  if (!TODOIST_STORE.getSleepScreenEnabled()) return;

  const uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!framebuffer) {
    LOG_ERR("TDA", "Framebuffer unavailable; sleep screen not updated");
    return;
  }
  // Same file and format the "Set Cover" action writes from the image viewer,
  // so SleepActivity's CUSTOM mode picks it up unchanged.
  if (!ScreenshotUtil::saveFramebufferAsBmp(SLEEP_SCREEN_PATH, framebuffer, renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR("TDA", "Failed to write %s", SLEEP_SCREEN_PATH);
    return;
  }
  LOG_DBG("TDA", "Sleep screen updated from the task list");

  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    // The wallpaper is only shown in CUSTOM mode; switching is what makes the
    // snapshot visible at all.
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    LOG_INF("TDA", "Sleep screen mode switched to custom");
  }
}

const char* TodoistActivity::errorText(const TodoistClient::Error error) {
  switch (error) {
    case TodoistClient::NO_TOKEN:
      return tr(STR_TODOIST_NO_TOKEN);
    case TodoistClient::AUTH_FAILED:
      return tr(STR_TODOIST_INVALID_TOKEN);
    case TodoistClient::SERVER_ERROR:
      return tr(STR_TODOIST_SERVER_ERROR);
    case TodoistClient::PARSE_ERROR:
      return tr(STR_TODOIST_BAD_RESPONSE);
    case TodoistClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

void TodoistActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // "Today DD-MM-YYYY" as the title, "Overdue: N" as the right-aligned
  // secondary label. Drawn through the theme's header so this screen keeps the
  // battery indicator and the chrome every other screen has.
  char date[12];
  formatDisplayDate(TODOIST_TASKS.getSyncDate(), date, sizeof(date));
  char title[48];
  snprintf(title, sizeof(title), "%s %s", tr(STR_TODAY), date);

  char overdue[32];
  snprintf(overdue, sizeof(overdue), "%s: %zu", tr(STR_OVERDUE), TODOIST_TASKS.getOverdueCount());

  char status[64];
  if (TODOIST_TASKS.hasPending()) {
    // Completions that have not reached the server yet; they push on the next sync.
    char waiting[32];
    snprintf(waiting, sizeof(waiting), tr(STR_TODOIST_PENDING_COMPLETIONS),
             static_cast<int>(TODOIST_TASKS.getPendingIds().size()));
    snprintf(status, sizeof(status), "%s | %s", waiting, overdue);
  } else {
    snprintf(status, sizeof(status), "%s", overdue);
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, status);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = taskCount();

  // One centered message per non-list state; the failure message covers the
  // list until dismissed.
  if (state == State::SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TODOIST_SYNCING));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                              TODOIST_TASKS.hasSynced() ? tr(STR_TODOIST_NO_TASKS) : tr(STR_TODOIST_NEVER_SYNCED));
  } else {
    const auto& tasks = TODOIST_TASKS.getTasks();
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
                 [&tasks](int index) -> std::string { return tasks[index].content; });
  }

  // Select is context-dependent: it completes a task when there is one to
  // complete, dismisses a failure, and otherwise triggers the sync a hold
  // would. Up/Down only make sense over a list.
  const char* confirmLabel = tr(STR_SYNC_NOW);
  if (state == State::SYNCING) {
    confirmLabel = "";
  } else if (state == State::FAILED) {
    confirmLabel = tr(STR_OK_BUTTON);
  } else if (itemCount > 0) {
    confirmLabel = tr(STR_COMPLETE_TASK);
  }
  const bool navigable = state == State::LIST && itemCount > 0;
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
