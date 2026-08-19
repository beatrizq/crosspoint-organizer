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
  for (const auto& calendar : selectedCalendars) {
    JsonObject entry = cals.add<JsonObject>();
    entry["id"] = calendar.id;
    entry["name"] = calendar.name;
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
  // Entries are {id, name} objects. A bare string is the pre-name format, and is
  // also the easiest thing to hand-edit in, so it is still accepted: the id is
  // taken and the name left empty, which falls back to showing the id until the
  // picker records one. Reading one asks for a resave so the file converges on
  // the object form.
  bool migratedCalendars = false;
  for (JsonVariantConst value : cals) {
    if (selectedCalendars.size() >= MAX_CALENDARS) break;
    if (value.is<JsonObjectConst>()) {
      const char* id = value["id"] | "";
      if (id[0] == '\0') continue;
      selectedCalendars.push_back(
          SelectedCalendar{clamped(id, MAX_CALENDAR_ID_LEN), clamped(value["name"] | "", MAX_CALENDAR_NAME_LEN)});
      continue;
    }
    const char* id = value | "";
    if (id[0] == '\0') continue;
    selectedCalendars.push_back(SelectedCalendar{clamped(id, MAX_CALENDAR_ID_LEN), std::string()});
    migratedCalendars = true;
  }
  if (migratedCalendars) {
    LOG_DBG("GCS", "Calendar ids without names found, resaving in the object form");
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
  return std::find_if(selectedCalendars.begin(), selectedCalendars.end(),
                      [&id](const SelectedCalendar& calendar) { return calendar.id == id; }) != selectedCalendars.end();
}

void GCalStore::toggleCalendar(const std::string& id, const std::string& name) {
  const auto found = std::find_if(selectedCalendars.begin(), selectedCalendars.end(),
                                  [&id](const SelectedCalendar& calendar) { return calendar.id == id; });
  if (found != selectedCalendars.end()) {
    selectedCalendars.erase(found);
    return;
  }
  if (selectedCalendars.size() >= MAX_CALENDARS) {
    LOG_ERR("GCS", "Calendar selection full (%zu), ignoring %s", MAX_CALENDARS, id.c_str());
    return;
  }
  selectedCalendars.push_back(SelectedCalendar{clamped(id, MAX_CALENDAR_ID_LEN), clamped(name, MAX_CALENDAR_NAME_LEN)});
}
