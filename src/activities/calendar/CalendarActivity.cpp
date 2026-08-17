#include "CalendarActivity.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
// Abbreviated weekday and month names, indexed by civil::weekdayFromDate (0 =
// Sunday) and month-1. Deliberately not translated: they are drawn in a narrow
// subtitle where a long localised name would push the time off the row, and
// three-letter forms read the same across the Latin-script languages the device
// ships. Static const so they live in flash, not DRAM.
static const char* const WEEKDAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTH_NAMES[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
}  // namespace

CalendarActivity::CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Calendar", renderer, mappedInput) {}

void CalendarActivity::onEnter() {
  Activity::onEnter();
  GCAL_EVENTS.loadFromFile();
  selectedIndex = 0;
  requestUpdate();
}

void CalendarActivity::onExit() {
  Activity::onExit();

  // Same teardown as the Todoist and KOReader sync screens: drop the
  // association, then reboot silently to home so the WiFi/TLS heap
  // fragmentation goes with it. The mode check keeps a cancelled Wi-Fi picker
  // (radio never brought up) from costing a reboot.
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

int CalendarActivity::eventCount() const { return static_cast<int>(GCAL_EVENTS.getEvents().size()); }

void CalendarActivity::loop() {
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
    } else {
      // Events are read-only here, so Select has nothing to do on a row: a
      // press and a hold both mean "sync".
      startSync();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int itemCount = eventCount();
  if (state != State::LIST || itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, true)) {
    case ListTouchResult::Activated:
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, true);
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

void CalendarActivity::startSync() {
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

void CalendarActivity::performSync() {
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
    LOG_ERR("CAL", "Token refresh failed: %s", GCalAuth::errorString(authError));
  } else if (today == civil::NO_DATE) {
    // Without a date there is no window to ask for, and guessing would silently
    // show the wrong month.
    LOG_ERR("CAL", "No Date header on the token response; cannot anchor the window");
    error = GCalClient::PARSE_ERROR;
  } else {
    const uint16_t lastDay = static_cast<uint16_t>(today + GCAL_WINDOW_DAYS - 1);
    fetched.reserve(GCAL_MAX_EVENTS);
    for (const auto& calendarId : GCAL_STORE.getSelectedCalendars()) {
      resetTaskWatchdogIfSubscribed();
      error = GCalClient::fetchEvents(accessToken, calendarId, today, lastDay, fetched);
      resetTaskWatchdogIfSubscribed();
      if (error != GCalClient::OK) {
        LOG_ERR("CAL", "Fetch failed for %s: %s", calendarId.c_str(), GCalClient::errorString(error));
        break;
      }
      if (fetched.size() >= GCAL_MAX_EVENTS) {
        LOG_INF("CAL", "Event cap (%zu) reached; later calendars not fetched", GCAL_MAX_EVENTS);
        break;
      }
    }
  }

  // Drop the radio before touching the SD card and repainting; the full
  // teardown happens on the silent reboot in onExit().
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
      selectedIndex = 0;
    } else {
      state = State::FAILED;
      statusMessage = errorText(error);
    }
  }
  GCAL_EVENTS.saveToFile();
  requestUpdate(true);
}

void CalendarActivity::formatEventWhen(const int index, char* out, const size_t outSize) const {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  const auto& events = GCAL_EVENTS.getEvents();
  if (index < 0 || static_cast<size_t>(index) >= events.size()) return;
  const GCalEvent& event = events[index];

  const uint8_t weekday = civil::weekdayFromDate(event.date);
  const uint8_t month = civil::monthFromDate(event.date);
  const uint8_t day = civil::dayOfMonthFromDate(event.date);
  if (month == 0) return;

  char when[24];
  snprintf(when, sizeof(when), "%s %u %s", WEEKDAY_NAMES[weekday % 7], static_cast<unsigned>(day),
           MONTH_NAMES[(month - 1) % 12]);

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

const char* CalendarActivity::errorText(const GCalClient::Error error) {
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

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // "Calendar" as the title, with the window's first day and the count of
  // events on it as the right-aligned secondary label.
  char status[48];
  if (GCAL_EVENTS.hasSynced()) {
    const uint16_t syncDate = GCAL_EVENTS.getSyncDate();
    snprintf(status, sizeof(status), "%s %u %s  ·  %s: %zu", WEEKDAY_NAMES[civil::weekdayFromDate(syncDate) % 7],
             static_cast<unsigned>(civil::dayOfMonthFromDate(syncDate)),
             MONTH_NAMES[(civil::monthFromDate(syncDate) - 1) % 12], tr(STR_TODAY), GCAL_EVENTS.getTodayCount());
  } else {
    snprintf(status, sizeof(status), "%s", tr(STR_GCAL_NEVER_SYNCED));
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CALENDAR), status);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = eventCount();

  // One centered message per non-list state; the failure message covers the
  // list until dismissed.
  if (state == State::SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GCAL_SYNCING));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                              GCAL_EVENTS.hasSynced() ? tr(STR_GCAL_NO_EVENTS) : tr(STR_GCAL_NEVER_SYNCED));
  } else {
    const auto& events = GCAL_EVENTS.getEvents();
    // Title is the event, subtitle is when: a date with no events contributes
    // no row at all, which is what makes a month fit in a scrollable list.
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&events](int index) -> std::string { return events[index].summary; },
        [this](int index) -> std::string {
          char when[48];
          formatEventWhen(index, when, sizeof(when));
          return std::string(when);
        });
  }

  const char* confirmLabel = state == State::SYNCING ? "" : tr(STR_GCAL_SYNC_NOW);
  if (state == State::FAILED) confirmLabel = tr(STR_OK_BUTTON);
  const bool navigable = state == State::LIST && itemCount > 0;
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
