#include "OrganizerActivity.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
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

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

// The dither patterns have period 2 in logical space, so a 1px rule lands on
// either an "on" or an "off" phase depending on its y parity - with an odd row
// height that made the separator appear on every other row. Two pixels covers
// both phases whatever the parity.
constexpr int SEPARATOR_HEIGHT = 2;

// Abbreviated weekday and month names, indexed by civil::weekdayFromDate (0 =
// Sunday) and month-1. Deliberately not translated: they are drawn in a narrow
// subtitle where a long localised name would push the time off the row, and
// three-letter forms read the same across the Latin-script languages the device
// ships. Static const so they live in flash, not DRAM.
static const char* const WEEKDAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTH_NAMES[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// "Mon 17 Aug", or "--" when the date is unknown. Both tabs date themselves the
// same way: they answer the same question, and two formats on one screen read as
// an inconsistency rather than a distinction.
void formatDayLabel(const uint16_t date, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  const uint8_t month = civil::monthFromDate(date);
  if (date == civil::NO_DATE || month == 0) {
    snprintf(out, outSize, "--");
    return;
  }
  snprintf(out, outSize, "%s %u %s", WEEKDAY_NAMES[civil::weekdayFromDate(date) % 7],
           static_cast<unsigned>(civil::dayOfMonthFromDate(date)), MONTH_NAMES[(month - 1) % 12]);
}

// Later of two "YYYY-MM-DD" dates. Today only ever moves forward, so picking the
// newest of the available sources is what keeps a partial one from regressing.
// ISO dates order correctly as plain strings, and "" loses to any real date.
const std::string& laterDate(const std::string& a, const std::string& b) { return b > a ? b : a; }
}  // namespace

OrganizerActivity::OrganizerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Organizer", renderer, mappedInput) {}

void OrganizerActivity::onEnter() {
  Activity::onEnter();
  TODOIST_TASKS.loadFromFile();
  GCAL_EVENTS.loadFromFile();
  selectedIndex = 0;
  requestUpdate();
}

void OrganizerActivity::onExit() {
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

// -- shared -----------------------------------------------------------------

int OrganizerActivity::rowCount() const {
  return tab == Tab::TASKS ? static_cast<int>(TODOIST_TASKS.getTasks().size())
                           : static_cast<int>(GCAL_EVENTS.getEvents().size());
}

int OrganizerActivity::titleFontId() const {
  // Small is the size these screens always drew at; Large is the only larger UI
  // font there is. The default arm also absorbs a stale persisted value from
  // when this setting had three options.
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? UI_10_FONT_ID : UI_12_FONT_ID;
}

int OrganizerActivity::subtitleFontId() const {
  // One step below the title, so the date stays subordinate to the event.
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? SMALL_FONT_ID : UI_10_FONT_ID;
}

int OrganizerActivity::rowPadding() const {
  // Proportional to the text: a fixed gap that suits 10pt leaves the rows
  // looking cramped once the font grows, which is the point of the setting.
  return std::max(6, renderer.getLineHeight(titleFontId()) * 2 / 5);
}

int OrganizerActivity::listRowHeight() const {
  // Only the calendar tab carries a second line, so task rows stay compact.
  const int titleH = renderer.getLineHeight(titleFontId());
  const int subH = tab == Tab::CALENDAR ? renderer.getLineHeight(subtitleFontId()) : 0;
  return titleH + subH + rowPadding();
}

void OrganizerActivity::switchTab(const Tab next) {
  if (tab == next) return;
  tab = next;
  // Row indices mean different things per tab; start at the top of the new one.
  selectedIndex = 0;
  state = State::LIST;
  statusMessage = nullptr;
}

// -- tasks tab --------------------------------------------------------------

void OrganizerActivity::completeSelectedTask() {
  const int row = selectedRow();
  if (row < 0 || row >= rowCount()) return;

  LOG_DBG("ORG", "Completing task: %s", TODOIST_TASKS.getTasks()[row].content.c_str());
  {
    // The render task reads the task list; hold the lock across the removal so
    // it never paints a half-updated list.
    RenderLock lock(*this);
    // Queued locally and pushed on the next sync, so completing works with the
    // radio off; the row leaves the list immediately either way.
    TODOIST_TASKS.completeTaskAt(static_cast<size_t>(row));
    const int remaining = rowCount();
    if (selectedRow() >= remaining) selectedIndex = remaining;
    if (selectedIndex < 1) selectedIndex = remaining > 0 ? 1 : 0;
  }
  TODOIST_TASKS.saveToFile();
  requestUpdate(true);
}

void OrganizerActivity::startTaskSync() {
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
    performTaskSync();
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
                           performTaskSync();
                         });
}

bool OrganizerActivity::resolveTodayDate(std::string& outDate) const {
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
    LOG_DBG("ORG", "NTP sync timed out; relying on the response date");
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

void OrganizerActivity::performTaskSync() {
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
      LOG_ERR("ORG", "Push failed for %s: %s", id.c_str(), TodoistClient::errorString(error));
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
    LOG_ERR("ORG", "Today unresolved: no dated tasks, no NTP, no previous sync");
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
      selectedIndex = rowCount() > 0 ? 1 : 0;
    } else {
      state = State::FAILED;
      statusMessage = taskErrorText(error);
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

void OrganizerActivity::saveSleepWallpaper() const {
  if (!TODOIST_STORE.getSleepScreenEnabled()) {
    // Logged so a sync that leaves the sleep screen untouched is distinguishable
    // from one where this was never reached.
    LOG_DBG("ORG", "Sleep screen disabled in settings; wallpaper not updated");
    return;
  }

  const uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!framebuffer) {
    LOG_ERR("ORG", "Framebuffer unavailable; sleep screen not updated");
    return;
  }
  // Same file and format the "Set Cover" action writes from the image viewer,
  // so SleepActivity's CUSTOM mode picks it up unchanged.
  if (!ScreenshotUtil::saveFramebufferAsBmp(SLEEP_SCREEN_PATH, framebuffer, renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR("ORG", "Failed to write %s", SLEEP_SCREEN_PATH);
    return;
  }
  LOG_DBG("ORG", "Sleep screen updated from the task list");

  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    // The wallpaper is only shown in CUSTOM mode; switching is what makes the
    // snapshot visible at all.
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    LOG_INF("ORG", "Sleep screen mode switched to custom");
  }
}

const char* OrganizerActivity::taskErrorText(const TodoistClient::Error error) {
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

// -- calendar tab -----------------------------------------------------------

void OrganizerActivity::startCalendarSync() {
  if (!GCAL_STORE.hasClientCredentials() || !GCAL_STORE.isLinked()) {
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_GCAL_NOT_LINKED);
    }
    requestUpdate(true);
    return;
  }
  if (GCAL_STORE.getSelectedCalendars().empty()) {
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_GCAL_NO_CALENDARS);
    }
    requestUpdate(true);
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  requestUpdate();

  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    performCalendarSync();
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
                           performCalendarSync();
                         });
}

void OrganizerActivity::performCalendarSync() {
  // The refresh serves two purposes: it mints the access token every request
  // needs, and its HTTP Date header is the device's clock. Most boards have no
  // RTC and SNTP can be blocked on a given network, so anchoring the window on
  // a header that cannot fail when the request succeeded is what makes "today
  // and the next 30 days" mean anything.
  std::string accessToken;
  uint16_t today = civil::NO_DATE;
  const GCalAuth::Error authError = GCalAuth::refreshAccessToken(accessToken, today);

  GCalClient::Error error = GCalClient::OK;
  std::vector<GCalEvent> fetched;

  if (authError != GCalAuth::OK) {
    LOG_ERR("ORG", "Token refresh failed: %s", GCalAuth::errorString(authError));
  } else if (today == civil::NO_DATE) {
    // Without a date there is no window to ask for, and guessing would silently
    // show the wrong month.
    LOG_ERR("ORG", "No Date header on the token response; cannot anchor the window");
    error = GCalClient::PARSE_ERROR;
  } else {
    const uint16_t lastDay = static_cast<uint16_t>(today + GCAL_WINDOW_DAYS - 1);
    fetched.reserve(GCAL_MAX_EVENTS);
    for (const auto& calendarId : GCAL_STORE.getSelectedCalendars()) {
      resetTaskWatchdogIfSubscribed();
      error = GCalClient::fetchEvents(accessToken, calendarId, today, lastDay, fetched);
      resetTaskWatchdogIfSubscribed();
      if (error != GCalClient::OK) {
        LOG_ERR("ORG", "Fetch failed for %s: %s", calendarId.c_str(), GCalClient::errorString(error));
        break;
      }
      if (fetched.size() >= GCAL_MAX_EVENTS) {
        LOG_INF("ORG", "Event cap (%zu) reached; later calendars not fetched", GCAL_MAX_EVENTS);
        break;
      }
    }
  }

  esp_wifi_stop();

  {
    RenderLock lock(*this);
    if (authError != GCalAuth::OK) {
      state = State::FAILED;
      statusMessage = authError == GCalAuth::INVALID_GRANT ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
    } else if (error == GCalClient::OK) {
      GCAL_EVENTS.setEvents(std::move(fetched), today);
      state = State::LIST;
      statusMessage = nullptr;
      selectedIndex = rowCount() > 0 ? 1 : 0;
    } else {
      state = State::FAILED;
      statusMessage = calendarErrorText(error);
    }
  }
  GCAL_EVENTS.saveToFile();
  requestUpdate(true);
}

void OrganizerActivity::formatEventWhen(const int index, char* out, const size_t outSize) const {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  const auto& events = GCAL_EVENTS.getEvents();
  if (index < 0 || static_cast<size_t>(index) >= events.size()) return;
  const GCalEvent& event = events[index];

  if (civil::monthFromDate(event.date) == 0) return;

  char when[16];
  formatDayLabel(event.date, when, sizeof(when));

  if (event.isAllDay()) {
    snprintf(out, outSize, "%s  %s", when, tr(STR_GCAL_ALL_DAY));
    return;
  }
  // An end time is not guaranteed, and a zero-length event reads better without
  // a range than as "09:30-09:30".
  if (event.endMin == civil::NO_TIME || event.endMin == event.startMin) {
    snprintf(out, outSize, "%s  %02u:%02u", when, static_cast<unsigned>(event.startMin / 60),
             static_cast<unsigned>(event.startMin % 60));
    return;
  }
  snprintf(out, outSize, "%s  %02u:%02u-%02u:%02u", when, static_cast<unsigned>(event.startMin / 60),
           static_cast<unsigned>(event.startMin % 60), static_cast<unsigned>(event.endMin / 60),
           static_cast<unsigned>(event.endMin % 60));
}

const char* OrganizerActivity::calendarErrorText(const GCalClient::Error error) {
  switch (error) {
    case GCalClient::NO_TOKEN:
      return tr(STR_GCAL_NOT_LINKED);
    case GCalClient::AUTH_FAILED:
      return tr(STR_GCAL_RELINK_NEEDED);
    case GCalClient::SERVER_ERROR:
      return tr(STR_GCAL_SERVER_ERROR);
    case GCalClient::PARSE_ERROR:
      return tr(STR_GCAL_BAD_RESPONSE);
    case GCalClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

// -- input ------------------------------------------------------------------

void OrganizerActivity::loop() {
  if (state == State::SYNCING) return;  // ignore input while the sync blocks

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::FAILED) {
      // Dismiss the failure message and fall back to whatever is cached.
      {
        RenderLock lock(*this);
        state = State::LIST;
        statusMessage = nullptr;
      }
      requestUpdate(true);
      return;
    }
    if (selectedIndex == 0) {
      // Tabs focused: Select cycles them, matching the settings screen.
      {
        RenderLock lock(*this);
        switchTab(tab == Tab::TASKS ? Tab::CALENDAR : Tab::TASKS);
      }
      requestUpdate(true);
      return;
    }
    if (tab == Tab::CALENDAR) {
      // Events are read-only here, so a row press has nothing to act on.
      startCalendarSync();
      return;
    }
    if (mappedInput.getHeldTime() >= LONG_PRESS_MS || rowCount() == 0) {
      // Hold syncs; with nothing to complete, a plain press syncs too.
      startTaskSync();
      return;
    }
    completeSelectedTask();
    return;
  }

  if (state != State::LIST) return;

  // Index 0 is the tab bar, so the navigable range is one longer than the list.
  const int navCount = rowCount() + 1;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int rowHeight = std::max(1, listRowHeight());
  const int pageItems = std::max(1, listHeight / rowHeight);

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    // Tabs first: they sit above the list and share the same tap stream.
    std::vector<TabInfo> tabs = {{tr(STR_ORGANIZER_TAB_TASKS), tab == Tab::TASKS},
                                 {tr(STR_ORGANIZER_TAB_CALENDAR), tab == Tab::CALENDAR}};
    int tappedTab = -1;
    const int tabTop = metrics.topPadding + metrics.headerHeight;
    if (GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs, tx, ty,
                              tappedTab)) {
      {
        RenderLock lock(*this);
        switchTab(tappedTab == 0 ? Tab::TASKS : Tab::CALENDAR);
      }
      requestUpdate(true);
      return;
    }
    // Rows are hit-tested against this screen's own row height, not the theme's:
    // the list is drawn here so the font size can follow the setting.
    if (ty >= listTop && ty < listTop + listHeight && rowCount() > 0) {
      const int pageStart = selectedRow() < 0 ? 0 : (selectedRow() / pageItems) * pageItems;
      const int tapped = pageStart + (ty - listTop) / rowHeight;
      if (tapped >= 0 && tapped < rowCount()) {
        selectedIndex = tapped + 1;
        requestUpdate();
      }
      return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = selectedIndex == 0 ? std::min(1, navCount - 1)
                                       : ButtonNavigator::nextPageIndex(selectedIndex, navCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, navCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, navCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, navCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, navCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, navCount);
    requestUpdate();
  });
}

// -- render -----------------------------------------------------------------

void OrganizerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Header: the screen's name, with the active tab's own summary on the right.
  char status[64];
  if (tab == Tab::TASKS) {
    char date[16];
    formatDayLabel(civil::dateFromIso(TODOIST_TASKS.getSyncDate().c_str()), date, sizeof(date));
    char overdue[32];
    snprintf(overdue, sizeof(overdue), "%s: %zu", tr(STR_OVERDUE), TODOIST_TASKS.getOverdueCount());
    if (TODOIST_TASKS.hasPending()) {
      char waiting[32];
      snprintf(waiting, sizeof(waiting), tr(STR_TODOIST_PENDING_COMPLETIONS),
               static_cast<int>(TODOIST_TASKS.getPendingIds().size()));
      snprintf(status, sizeof(status), "%s %s | %s", date, waiting, overdue);
    } else {
      snprintf(status, sizeof(status), "%s  ·  %s", date, overdue);
    }
  } else if (GCAL_EVENTS.hasSynced()) {
    char date[16];
    formatDayLabel(GCAL_EVENTS.getSyncDate(), date, sizeof(date));
    snprintf(status, sizeof(status), "%s  ·  %s: %zu", date, tr(STR_TODAY), GCAL_EVENTS.getTodayCount());
  } else {
    snprintf(status, sizeof(status), "%s", tr(STR_GCAL_NEVER_SYNCED));
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ORGANIZER), status);

  const std::vector<TabInfo> tabs = {{tr(STR_ORGANIZER_TAB_TASKS), tab == Tab::TASKS},
                                     {tr(STR_ORGANIZER_TAB_CALENDAR), tab == Tab::CALENDAR}};
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedIndex == 0);

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = rowCount();

  // One centered message per non-list state; the failure message covers the
  // list until dismissed.
  if (state == State::SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                              tab == Tab::TASKS ? tr(STR_TODOIST_SYNCING) : tr(STR_GCAL_SYNCING));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    const char* empty;
    if (tab == Tab::TASKS) {
      empty = TODOIST_TASKS.hasSynced() ? tr(STR_TODOIST_NO_TASKS) : tr(STR_TODOIST_NEVER_SYNCED);
    } else {
      empty = GCAL_EVENTS.hasSynced() ? tr(STR_GCAL_NO_EVENTS) : tr(STR_GCAL_NEVER_SYNCED);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, empty);
  } else {
    // Drawn here rather than through GUI.drawList so the row font follows
    // SETTINGS.organizerFontSize; the theme's list draws at a fixed size.
    const int titleFont = titleFontId();
    const int subFont = subtitleFontId();
    const int rowHeight = std::max(1, listRowHeight());
    const int rowPad = rowPadding();
    const int pageItems = std::max(1, listHeight / rowHeight);
    const int pageStart = selectedRow() < 0 ? 0 : (selectedRow() / pageItems) * pageItems;
    const int textX = metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;

    const auto& tasks = TODOIST_TASKS.getTasks();
    const auto& events = GCAL_EVENTS.getEvents();

    for (int row = 0; row < pageItems; row++) {
      const int index = pageStart + row;
      if (index >= itemCount) break;
      const int rowY = listTop + row * rowHeight;
      const bool selected = index == selectedRow();

      if (selected) {
        renderer.fillRect(0, rowY, pageWidth, rowHeight);
      }
      // Selected rows invert: the fill is black, so the text has to be white.
      const bool ink = !selected;

      const std::string& title = tab == Tab::TASKS ? tasks[index].content : events[index].summary;
      const auto shownTitle = renderer.truncatedText(titleFont, title.c_str(), textWidth);
      renderer.drawText(titleFont, textX, rowY + rowPad / 2, shownTitle.c_str(), ink);

      if (tab == Tab::CALENDAR) {
        char when[48];
        formatEventWhen(index, when, sizeof(when));
        const auto shownWhen = renderer.truncatedText(subFont, when, textWidth);
        renderer.drawText(subFont, textX, rowY + rowPad / 2 + renderer.getLineHeight(titleFont), shownWhen.c_str(),
                          ink);
      }

      // Soft rule between entries, so a wrapped title cannot be mistaken for the
      // start of the next one. Dithered rather than solid: a black hairline
      // carries more weight on e-ink than the text it is separating.
      //
      // Skipped either side of the selected row, whose fill already bounds it,
      // and after the last row on the page, where it would underline nothing.
      const bool nextSelected = (index + 1) == selectedRow();
      const bool lastOnPage = row + 1 >= pageItems || index + 1 >= itemCount;
      if (!selected && !nextSelected && !lastOnPage) {
        renderer.fillRectDither(textX, rowY + rowHeight - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                                Color::LightGray);
      }
    }
  }

  // Select is context-dependent: it cycles tabs when they are focused, completes
  // a task when there is one, and otherwise syncs the tab being shown.
  const char* confirmLabel;
  if (state == State::SYNCING) {
    confirmLabel = "";
  } else if (state == State::FAILED) {
    confirmLabel = tr(STR_OK_BUTTON);
  } else if (selectedIndex == 0) {
    // With the tabs focused, Select switches to the other one - so it is labelled
    // with where it goes rather than with what it is.
    confirmLabel = tab == Tab::TASKS ? tr(STR_ORGANIZER_TAB_CALENDAR) : tr(STR_ORGANIZER_TAB_TASKS);
  } else if (tab == Tab::TASKS && itemCount > 0) {
    confirmLabel = tr(STR_COMPLETE_TASK);
  } else {
    confirmLabel = tr(STR_SYNC_NOW);
  }
  const bool navigable = state == State::LIST;
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
