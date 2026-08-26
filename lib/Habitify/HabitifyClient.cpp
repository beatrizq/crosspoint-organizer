#include "HabitifyClient.h"

#include <Arduino.h>
#include <CivilTime.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

#include <cstdio>
#include <utility>

#include "HabitifyJournalParser.h"
#include "HabitifyStore.h"

int HabitifyClient::lastHttpCode = 0;

namespace {

constexpr char API_BASE[] = "https://api.habitify.me/v2";

// Same TLS heap gate as TodoistClient, GCalClient and YnabClient.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("HBC", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap, MIN_FREE_FOR_TLS,
            maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

// Percent-encodes everything outside the unreserved set. Habit ids go in a path
// segment and come from the API rather than being typed, but encoding them costs
// nothing and stops an unexpected character reshaping the URL.
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

// The key goes in X-API-Key, not as a bearer token. See the header.
void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("X-API-Key", HABITIFY_STORE.getApiKey());
  http.addHeader("Accept", "application/json");
}

HabitifyClient::Error errorForStatus(const int httpCode) {
  if (httpCode <= 0) return HabitifyClient::NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return HabitifyClient::OK;
  // 401 and 403 cover both a bad key and a key on a plan without API access;
  // the API does not distinguish them, so neither can this.
  if (httpCode == 401 || httpCode == 403) return HabitifyClient::AUTH_FAILED;
  if (httpCode == 404) return HabitifyClient::NOT_FOUND;
  if (httpCode == 429) return HabitifyClient::RATE_LIMITED;
  return HabitifyClient::SERVER_ERROR;
}

// Sink for HabitifyJournalParser: appends habits until the cache cap is reached.
struct HabitCollector {
  std::vector<HabitifyHabit>* out;
};

void collectHabit(void* ctx, const HabitifyParsedHabit& parsed) {
  auto* collector = static_cast<HabitCollector*>(ctx);
  if (collector->out->size() >= HABITIFY_MAX_HABITS) return;

  HabitifyHabit habit;
  habit.id = parsed.id;
  // A habit with no name would draw a blank row; its id is at least selectable.
  habit.name = parsed.name[0] != '\0' ? parsed.name : parsed.id;
  habit.unitSymbol = parsed.unitSymbol;
  habit.current = parsed.current;
  habit.target = parsed.target;
  habit.completedByStatus = parsed.completed;
  collector->out->push_back(std::move(habit));
}

}  // namespace

HabitifyClient::Error HabitifyClient::fetchJournal(std::vector<HabitifyHabit>& outHabits, uint16_t& outDate) {
  lastHttpCode = 0;
  outHabits.clear();
  if (!HABITIFY_STORE.hasApiKey()) {
    LOG_DBG("HBC", "No API key configured");
    return NO_KEY;
  }
  if (insufficientHeap()) return LOW_MEMORY;

  outHabits.reserve(HABITIFY_MAX_HABITS);
  HabitCollector collector{&outHabits};

  // On the heap, not the stack: the parser embeds the streaming tokenizer's
  // 512-byte buffer plus its field buffers, well past what a task stack here
  // should carry.
  auto parser = makeUniqueNoThrow<HabitifyJournalParser>(collectHabit, &collector);
  if (!parser) {
    LOG_ERR("HBC", "OOM: HabitifyJournalParser");
    return LOW_MEMORY;
  }

  // No date parameter: the journal defaults to today in the account's own
  // timezone, which is more reliable than a date this device computed - most
  // boards have no RTC.
  const std::string url = std::string(API_BASE) + "/habits/journal";

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("HBC", "Bad journal URL");
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);

  // Streamed into the parser as it arrives: a full journal runs to tens of KB of
  // JSON, which would not fit alongside a live TLS session.
  const int httpCode = http.GET([&parser](const uint8_t* data, const size_t len) {
    parser->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });

  // Read before end(): the parsed headers belong to this connection.
  const std::string dateHeader = http.getHeader("date");
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("HBC", "journal: %d (%zu habits)", httpCode, parser->habitCount());

  const Error status = errorForStatus(httpCode);
  if (status != OK) {
    outHabits.clear();
    return status;
  }
  if (parser->hasError()) {
    LOG_ERR("HBC", "Malformed journal JSON");
    outHabits.clear();
    return PARSE_ERROR;
  }

  if (!dateHeader.empty()) {
    const uint16_t date = civil::dateFromHttpHeader(dateHeader.c_str());
    if (date != civil::NO_DATE) outDate = date;
  }
  return OK;
}

HabitifyClient::Error HabitifyClient::addLog(const std::string& habitId, const std::string& unitSymbol,
                                             const float value) {
  lastHttpCode = 0;
  if (!HABITIFY_STORE.hasApiKey()) return NO_KEY;
  if (habitId.empty() || unitSymbol.empty() || value <= 0.0f) return NOT_FOUND;
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = std::string(API_BASE) + "/habits/" + urlEncode(habitId) + "/logs";

  // Built by hand rather than through ArduinoJson: two fields, one of which is a
  // number, is not worth a document. %g so a whole number goes as "3" rather
  // than "3.000000", and a fractional one still survives.
  char body[96];
  snprintf(body, sizeof(body), "{\"unitSymbol\":\"%s\",\"value\":%g}", unitSymbol.c_str(), static_cast<double>(value));

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("HBC", "Bad log URL for habit %s", habitId.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  http.addHeader("Content-Type", "application/json");

  const int httpCode = http.POST(body);
  http.end();
  lastHttpCode = httpCode;
  LOG_DBG("HBC", "log %s += %g: %d", habitId.c_str(), static_cast<double>(value), httpCode);
  return errorForStatus(httpCode);
}

const char* HabitifyClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "ok";
    case NO_KEY:
      return "no API key";
    case NETWORK_ERROR:
      return "network error";
    case AUTH_FAILED:
      return "API key rejected (or plan without API access)";
    case NOT_FOUND:
      return "habit not found";
    case RATE_LIMITED:
      return "rate limited";
    case SERVER_ERROR:
      return "Habitify server error";
    case PARSE_ERROR:
      return "unexpected response";
    case LOW_MEMORY:
      return "not enough memory to sync";
    default:
      return "unknown error";
  }
}
