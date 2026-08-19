#include "CalendarActivity.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>
#include <vector>

#include "OrganizerLabels.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

void CalendarActivity::loadCaches() { GCAL_EVENTS.loadFromFile(); }

const char* CalendarActivity::screenTitle() const { return tr(STR_ORGANIZER_TAB_CALENDAR); }

const char* CalendarActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::ALL:
      return tr(STR_CALENDAR_TAB_ALL);
  }
  return "";
}

// -- rows -------------------------------------------------------------------

int CalendarActivity::rowCount() const { return static_cast<int>(GCAL_EVENTS.getEvents().size()); }

void CalendarActivity::drawRow(const RowLayout& layout) const {
  const auto& events = GCAL_EVENTS.getEvents();
  if (layout.index < 0 || static_cast<size_t>(layout.index) >= events.size()) return;

  const auto shownTitle =
      renderer.truncatedText(layout.titleFont, events[static_cast<size_t>(layout.index)].summary.c_str(), layout.width);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownTitle.c_str(), layout.ink);

  char when[48];
  formatEventWhen(layout.index, when, sizeof(when));
  const auto shownWhen = renderer.truncatedText(layout.subtitleFont, when, layout.width);
  renderer.drawText(layout.subtitleFont, layout.x, layout.textY + renderer.getLineHeight(layout.titleFont),
                    shownWhen.c_str(), layout.ink);
}

void CalendarActivity::formatStatus(char* out, const size_t outSize) const {
  if (!GCAL_EVENTS.hasSynced()) {
    snprintf(out, outSize, "%s", tr(STR_GCAL_NEVER_SYNCED));
    return;
  }
  char date[16];
  organizer::formatDayLabel(GCAL_EVENTS.getSyncDate(), date, sizeof(date));
  snprintf(out, outSize, "%s  ·  %s: %zu", date, tr(STR_TODAY), GCAL_EVENTS.getTodayCount());
}

const char* CalendarActivity::emptyMessage() const {
  return GCAL_EVENTS.hasSynced() ? tr(STR_GCAL_NO_EVENTS) : tr(STR_GCAL_NEVER_SYNCED);
}

const char* CalendarActivity::syncingMessage() const { return tr(STR_GCAL_SYNCING); }

// -- sync -------------------------------------------------------------------

void CalendarActivity::startSync() {
  if (!GCAL_STORE.hasClientCredentials() || !GCAL_STORE.isLinked()) {
    failSync(tr(STR_GCAL_NOT_LINKED));
    return;
  }
  if (GCAL_STORE.getSelectedCalendars().empty()) {
    failSync(tr(STR_GCAL_NO_CALENDARS));
    return;
  }
  runSync([this] { performCalendarSync(); });
}

void CalendarActivity::performCalendarSync() {
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
    for (const auto& calendar : GCAL_STORE.getSelectedCalendars()) {
      resetTaskWatchdogIfSubscribed();
      error = GCalClient::fetchEvents(accessToken, calendar.id, today, lastDay, fetched);
      resetTaskWatchdogIfSubscribed();
      if (error != GCalClient::OK) {
        LOG_ERR("CAL", "Fetch failed for %s: %s", calendar.id.c_str(), GCalClient::errorString(error));
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
  tearDownRadio();

  const char* failure = nullptr;
  if (authError != GCalAuth::OK) {
    failure = authError == GCalAuth::INVALID_GRANT ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
  } else if (error != GCalClient::OK) {
    failure = calendarErrorText(error);
  } else {
    RenderLock lock(*this);
    GCAL_EVENTS.setEvents(std::move(fetched), today);
  }
  finishSync(failure);
  GCAL_EVENTS.saveToFile();
}

void CalendarActivity::formatEventWhen(const int index, char* out, const size_t outSize) const {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  const auto& events = GCAL_EVENTS.getEvents();
  if (index < 0 || static_cast<size_t>(index) >= events.size()) return;
  const GCalEvent& event = events[static_cast<size_t>(index)];

  if (civil::monthFromDate(event.date) == 0) return;

  char when[16];
  organizer::formatDayLabel(event.date, when, sizeof(when));

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

const char* CalendarActivity::calendarErrorText(const GCalClient::Error error) {
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
