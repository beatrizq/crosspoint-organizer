#include "GCalClient.h"

#include <Arduino.h>
#include <CivilTime.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <utility>

#include "GCalCalendarsParser.h"
#include "GCalEventsParser.h"

int GCalClient::lastHttpCode = 0;

namespace {

constexpr char API_BASE[] = "https://www.googleapis.com/calendar/v3";

// Same TLS heap gate as TodoistClient and GCalAuth.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("GCC", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap, MIN_FREE_FOR_TLS,
            maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

// Percent-encodes everything outside the unreserved set. Calendar ids are
// email-shaped ("a@group.calendar.google.com") and go in a path segment;
// timestamps carry ':' and go in the query.
std::string urlEncode(const std::string& value) {
  // Not named HEX: Arduino's Print.h defines that as a macro (`#define HEX 16`).
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() + 8);
  for (const unsigned char c : value) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(HEX_DIGITS[c >> 4]);
      out.push_back(HEX_DIGITS[c & 0x0F]);
    }
  }
  return out;
}

GCalClient::Error errorForStatus(const int httpCode) {
  if (httpCode <= 0) return GCalClient::NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return GCalClient::OK;
  if (httpCode == 401 || httpCode == 403) return GCalClient::AUTH_FAILED;
  return GCalClient::SERVER_ERROR;
}

// -- calendarList sink ------------------------------------------------------

struct CalendarCollector {
  std::vector<GCalClient::CalendarInfo>* out;
};

void collectCalendar(void* ctx, const char* id, const char* summary, const bool primary) {
  auto* collector = static_cast<CalendarCollector*>(ctx);
  if (collector->out->size() >= GCalClient::MAX_CALENDARS_LISTED) return;

  GCalClient::CalendarInfo info;
  info.id = id;
  // A calendar with no summary is unusual but possible; fall back to its id so
  // the picker never shows a blank row.
  info.summary = summary[0] != '\0' ? summary : id;
  info.primary = primary;
  collector->out->push_back(std::move(info));
}

// -- events sink ------------------------------------------------------------

struct EventCollector {
  std::vector<GCalEvent>* out;
  uint16_t fromDate;
  uint16_t toDate;
};

void collectEvent(void* ctx, const char* summary, const char* start, const char* end, const char* status) {
  auto* collector = static_cast<EventCollector*>(ctx);
  if (collector->out->size() >= GCAL_MAX_EVENTS) return;

  // Declined/removed instances of a recurring series still appear in the feed.
  if (strcmp(status, "cancelled") == 0) return;

  const uint16_t date = civil::dateFromIso(start);
  if (date == civil::NO_DATE) return;
  // The API filters by instant, so an event spanning midnight at the window edge
  // can arrive just outside the day range the screen shows.
  if (date < collector->fromDate || date > collector->toDate) return;

  GCalEvent event;
  event.summary = summary[0] != '\0' ? summary : "(no title)";
  if (event.summary.size() > GCalEvent::SUMMARY_MAX_LEN) {
    event.summary.resize(GCalEvent::SUMMARY_MAX_LEN);
  }
  event.date = date;
  event.startMin = civil::timeFromRfc3339(start);
  event.endMin = civil::timeFromRfc3339(end);
  collector->out->push_back(std::move(event));
}

}  // namespace

GCalClient::Error GCalClient::fetchCalendars(const std::string& accessToken, std::vector<CalendarInfo>& outCalendars) {
  lastHttpCode = 0;
  outCalendars.clear();
  if (accessToken.empty()) return NO_TOKEN;
  if (insufficientHeap()) return LOW_MEMORY;

  std::string url = API_BASE;
  url += "/users/me/calendarList?fields=items(id,summary,primary)&minAccessRole=reader&maxResults=";
  url += std::to_string(MAX_CALENDARS_LISTED);

  CalendarCollector collector{&outCalendars};
  GCalCalendarsParser parser(collectCalendar, &collector);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("GCC", "Bad calendarList URL");
    return NETWORK_ERROR;
  }
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Accept", "application/json");

  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("GCC", "calendarList: %d (%zu calendars)", httpCode, parser.calendarCount());

  const Error status = errorForStatus(httpCode);
  if (status != OK) {
    outCalendars.clear();
    return status;
  }
  if (parser.hasError()) {
    LOG_ERR("GCC", "Malformed calendarList JSON");
    outCalendars.clear();
    return PARSE_ERROR;
  }
  return OK;
}

GCalClient::Error GCalClient::fetchEvents(const std::string& accessToken, const std::string& calendarId,
                                          const uint16_t fromDate, const uint16_t toDate,
                                          std::vector<GCalEvent>& outEvents) {
  lastHttpCode = 0;
  if (accessToken.empty()) return NO_TOKEN;
  if (fromDate == civil::NO_DATE || toDate == civil::NO_DATE) {
    LOG_ERR("GCC", "Refusing to fetch events without a resolved date window");
    return PARSE_ERROR;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  // timeMax is exclusive, so ask for the day after the last one shown.
  char timeMin[21];
  char timeMax[21];
  civil::rfc3339FromDate(fromDate, timeMin, sizeof(timeMin));
  civil::rfc3339FromDate(static_cast<uint16_t>(toDate + 1), timeMax, sizeof(timeMax));
  if (timeMin[0] == '\0' || timeMax[0] == '\0') return PARSE_ERROR;

  std::string url = API_BASE;
  url += "/calendars/";
  url += urlEncode(calendarId);
  url += "/events?singleEvents=true&orderBy=startTime";
  url += "&fields=items(summary,status,start,end)";
  url += "&maxResults=";
  url += std::to_string(GCAL_MAX_EVENTS);
  url += "&timeMin=";
  url += urlEncode(timeMin);
  url += "&timeMax=";
  url += urlEncode(timeMax);

  EventCollector collector{&outEvents, fromDate, toDate};
  GCalEventsParser parser(collectEvent, &collector);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("GCC", "Bad events URL");
    return NETWORK_ERROR;
  }
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Accept", "application/json");

  // Streamed into the parser as it arrives: a month of events is tens of KB of
  // JSON, which would not fit alongside a live TLS session.
  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("GCC", "events: %d (%zu seen, %zu kept)", httpCode, parser.eventCount(), outEvents.size());

  const Error status = errorForStatus(httpCode);
  if (status != OK) return status;
  if (parser.hasError()) {
    LOG_ERR("GCC", "Malformed events JSON");
    return PARSE_ERROR;
  }
  return OK;
}

const char* GCalClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "ok";
    case NO_TOKEN:
      return "no access token";
    case NETWORK_ERROR:
      return "network error";
    case AUTH_FAILED:
      return "authorization rejected";
    case SERVER_ERROR:
      return "server error";
    case PARSE_ERROR:
      return "bad response";
    case LOW_MEMORY:
      return "low memory";
  }
  return "unknown";
}
