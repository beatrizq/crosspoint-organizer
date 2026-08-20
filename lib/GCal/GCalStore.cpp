#include "GCalStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {
std::string clamped(const std::string& value, const size_t maxLen) {
  return value.size() > maxLen ? value.substr(0, maxLen) : value;
}
}  // namespace

void GCalStore::toJson(JsonDocument& doc) const {
  // The client id is not a secret; leaving it readable makes a mistyped id
  // fixable from a PC without re-running the whole pairing flow.
  doc["clientId"] = clientId;
  doc["clientSecret_obf"] = obfuscation::obfuscateToBase64(clientSecret);
  doc["refreshToken_obf"] = obfuscation::obfuscateToBase64(refreshToken);

  JsonArray cals = doc["calendars"].to<JsonArray>();
  for (const auto& id : selectedCalendars) {
    cals.add(id);
  }
}

bool GCalStore::fromJson(JsonVariantConst doc) {
  clientId.clear();
  clientSecret.clear();
  refreshToken.clear();
  selectedCalendars.clear();

  clientId = clamped(doc["clientId"] | "", MAX_CLIENT_ID_LEN);

  // Both secrets decode the same way: a failure means the card was written by a
  // different device (the key is the MAC) or the file is corrupt. Dropping the
  // value is better than sending garbage to Google on every sync - the user is
  // told to re-link rather than left with a silently broken account.
  const auto decode = [](const char* encoded, const size_t maxLen, const char* what) -> std::string {
    if (encoded[0] == '\0') return std::string();
    bool ok = false;
    bool tooLong = false;
    std::string value = obfuscation::deobfuscateFromBase64(encoded, maxLen, &ok, &tooLong);
    if (!ok) {
      LOG_ERR("GCS", "%s failed to decode (%s), clearing", what, tooLong ? "too long" : "bad base64");
      value.clear();
    }
    return value;
  };

  clientSecret = decode(doc["clientSecret_obf"] | "", MAX_SECRET_LEN, "Client secret");
  refreshToken = decode(doc["refreshToken_obf"] | "", MAX_TOKEN_LEN, "Refresh token");

  // Plaintext fallback so credentials can be dropped into the JSON by hand from
  // a PC; they are re-saved obfuscated on load.
  bool needsResave = false;
  const char* plainSecret = doc["clientSecret"] | "";
  if (clientSecret.empty() && plainSecret[0] != '\0') {
    clientSecret = clamped(plainSecret, MAX_SECRET_LEN);
    needsResave = true;
  }
  const char* plainRefresh = doc["refreshToken"] | "";
  if (refreshToken.empty() && plainRefresh[0] != '\0') {
    refreshToken = clamped(plainRefresh, MAX_TOKEN_LEN);
    needsResave = true;
  }
  if (needsResave) {
    LOG_DBG("GCS", "Plaintext credentials found, resaving obfuscated");
    requestResave();
  }

  JsonArrayConst cals = doc["calendars"].as<JsonArrayConst>();
  selectedCalendars.reserve(std::min(cals.size(), MAX_CALENDARS));
  // Entries are plain id strings. An {id, name} object is also accepted: builds
  // between the Calendar screen growing a tab per calendar and losing them again
  // stored the display name alongside, and a card written by one of those would
  // otherwise read as an empty selection - silently unticking every calendar the
  // user had chosen. The name is dropped and a resave asked for, so the file
  // converges back on the plain form.
  bool objectCalendars = false;
  for (JsonVariantConst value : cals) {
    if (selectedCalendars.size() >= MAX_CALENDARS) break;
    const bool isObject = value.is<JsonObjectConst>();
    const char* id = isObject ? (value["id"] | "") : (value | "");
    if (id[0] == '\0') continue;
    if (isObject) objectCalendars = true;
    selectedCalendars.emplace_back(clamped(id, MAX_CALENDAR_ID_LEN));
  }
  if (objectCalendars) {
    LOG_DBG("GCS", "Calendar entries stored as objects, resaving as plain ids");
    requestResave();
  }

  LOG_DBG("GCS", "Loaded: client %s, linked %s, %zu calendars", clientId.empty() ? "no" : "yes",
          refreshToken.empty() ? "no" : "yes", selectedCalendars.size());
  return true;
}

void GCalStore::setClientId(const std::string& value) { clientId = clamped(value, MAX_CLIENT_ID_LEN); }

void GCalStore::setClientSecret(const std::string& value) { clientSecret = clamped(value, MAX_SECRET_LEN); }

void GCalStore::setRefreshToken(const std::string& value) { refreshToken = clamped(value, MAX_TOKEN_LEN); }

void GCalStore::unlink() {
  refreshToken.clear();
  selectedCalendars.clear();
}

bool GCalStore::isCalendarSelected(const std::string& id) const {
  return std::find(selectedCalendars.begin(), selectedCalendars.end(), id) != selectedCalendars.end();
}

void GCalStore::toggleCalendar(const std::string& id) {
  const auto found = std::find(selectedCalendars.begin(), selectedCalendars.end(), id);
  if (found != selectedCalendars.end()) {
    selectedCalendars.erase(found);
    return;
  }
  if (selectedCalendars.size() >= MAX_CALENDARS) {
    LOG_ERR("GCS", "Calendar selection full (%zu), ignoring %s", MAX_CALENDARS, id.c_str());
    return;
  }
  selectedCalendars.emplace_back(clamped(id, MAX_CALENDAR_ID_LEN));
}
