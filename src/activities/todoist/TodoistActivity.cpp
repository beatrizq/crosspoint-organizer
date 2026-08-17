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
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "util/ScreenshotUtil.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

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
}  // namespace

TodoistActivity::TodoistActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Todoist", renderer, mappedInput) {}

void TodoistActivity::onEnter() {
  UiListActivity::onEnter();
  TODOIST_TASKS.loadFromFile();
  rebuildRowItems();
  if (!TODOIST_STORE.hasToken()) {
    state = State::FAILED;
    statusMessage = tr(STR_TODOIST_NO_TOKEN);
  }
  // The base onEnter() already asked for a paint before the cache and state
  // above were in place; ask again so the first frame shows them.
  requestUpdate();
}

void TodoistActivity::onExit() {
  Activity::onExit();
  // rowItems' labels alias the cache's task strings; drop the aliases first.
  rowItems.clear();

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

int TodoistActivity::listCount() const { return static_cast<int>(rowItems.size()); }

void TodoistActivity::rebuildRowItems() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  rowItems.clear();
  rowItems.reserve(tasks.size());
  for (const auto& task : tasks) {
    fui::ListItem item;
    item.label = task.content.c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void TodoistActivity::activateIndex(const int index) {
  // The row action is "complete": the interaction table can deliver an index
  // captured before a completion shrank the list.
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  moveSelectionTo(index);
  completeSelected();
}

void TodoistActivity::completeSelected() {
  const int selected = nav.selected;
  if (selected < 0 || selected >= listCount()) return;

  LOG_DBG("TDA", "Completing task: %s", TODOIST_TASKS.getTasks()[selected].content.c_str());
  {
    // The interaction table still indexes the pre-removal rows; stop routing
    // touches against it until the next render republishes.
    closeRouting();
    RenderLock lock(*this);
    // Queued locally and pushed on the next sync, so completing works with the
    // radio off; the row leaves the list immediately either way.
    TODOIST_TASKS.completeTaskAt(static_cast<size_t>(selected));
    rebuildRowItems();
    if (nav.selected >= listCount()) nav.selected = listCount() - 1;
    if (nav.selected < 0) nav.selected = 0;
    nav.follow(listCount());
  }
  TODOIST_TASKS.saveToFile();
  requestUpdate(true);
}

bool TodoistActivity::handleButtons() {
  if (state == State::SYNCING) return true;  // ignore input while the sync blocks

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::FAILED) {
      // Dismiss the failure message and fall back to whatever is cached.
      {
        RenderLock lock(*this);
        state = State::LIST;
        statusMessage = nullptr;
      }
      requestUpdate(true);
    } else if (mappedInput.getHeldTime() >= LONG_PRESS_MS || rowItems.empty()) {
      // Hold syncs; with nothing to complete, a plain press syncs too.
      startSync();
    } else {
      completeSelected();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return true;
  }

  return false;
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
  // Most boards (X3/X4 included) have no RTC, so the date the header shows and
  // the date overdue flagging compares against both come from this sync.
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < NTP_POLL_ATTEMPTS && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(100);
    resetTaskWatchdogIfSubscribed();
  }
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    LOG_ERR("TDA", "NTP sync timed out; reusing last known date");
    outDate = TODOIST_TASKS.getSyncDate();
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
  std::string today;
  resolveTodayDate(today);

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
  if (error == TodoistClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::fetchTodayTasks(today, fetched);
    resetTaskWatchdogIfSubscribed();
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
      nav.reset();
    } else {
      state = State::FAILED;
      statusMessage = errorText(error);
    }
    rebuildRowItems();
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

void TodoistActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();

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

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title, status);
}

void TodoistActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // centeredText() centers in the whole remaining band, so these states are
  // one message each; the failure message covers the list until dismissed.
  if (state == State::SYNCING) {
    screen.centeredText(tr(STR_TODOIST_SYNCING), screen.theme().bodyText);
    return;
  }
  if (state == State::FAILED) {
    screen.centeredText(statusMessage, screen.theme().bodyText);
    return;
  }
  if (rowItems.empty()) {
    screen.centeredText(TODOIST_TASKS.hasSynced() ? tr(STR_TODOIST_NO_TASKS) : tr(STR_TODOIST_NEVER_SYNCED),
                        screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  // Task titles are sentences, not labels: wrap to a second line rather than
  // truncating, the way the list reads on paper.
  fui::TextStyle label = screen.theme().bodyText;
  label.maxLines = 2;
  props.labelText = label;
  syncListViewport(screen, props);
  screen.list(props);
}

void TodoistActivity::drawFooter() {
  // Select is context-dependent: it completes a task when there is one to
  // complete, dismisses a failure, and otherwise triggers the sync a hold
  // would. Up/Down only make sense over a list.
  const char* confirmLabel = tr(STR_SYNC_NOW);
  if (state == State::SYNCING) {
    confirmLabel = "";
  } else if (state == State::FAILED) {
    confirmLabel = tr(STR_OK_BUTTON);
  } else if (!rowItems.empty()) {
    confirmLabel = tr(STR_COMPLETE_TASK);
  }
  const bool navigable = state == State::LIST && !rowItems.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
