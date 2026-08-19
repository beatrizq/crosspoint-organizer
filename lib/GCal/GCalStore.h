#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

/**
 * Singleton holding the Google OAuth credentials and calendar selection.
 *
 * The client id and secret come from a Google Cloud project the device's owner
 * creates: this is an installed-app client used in testing mode, so the "secret"
 * is not a shared production credential and Google's app-verification review
 * does not apply. They are entered once and kept here rather than compiled in,
 * so the firmware carries no credentials and can be rebuilt or shared freely.
 *
 * The refresh token is XOR-obfuscated with the device's hardware MAC and
 * base64-encoded, the same scheme as the Todoist, KOReader and OPDS
 * credentials: not cryptographically secure, but it keeps a long-lived token
 * out of plain sight on a card that gets mounted on a PC, and it cannot be
 * decoded on another chip.
 *
 * The access token is deliberately NOT persisted. It expires in an hour, so
 * writing it would burn SD cycles for something almost always stale by the next
 * sync; it lives in RAM for the life of one sync and is re-minted from the
 * refresh token each time.
 */
/**
 * One ticked calendar: the id the fetch asks for, and the name to show for it.
 *
 * The name is stored because the Calendar screen labels a tab per calendar and
 * has to do it offline. Only GCalClient::fetchCalendars knows the display name,
 * and that needs the radio, so the picker records it at selection time; a file
 * written before names were stored - or hand-edited as a bare id - leaves it
 * empty and falls back to the id until the picker is next visited.
 */
struct SelectedCalendar {
  std::string id;
  std::string name;  // Display name, or "" when it was never recorded

  // What the UI should show for this calendar.
  const std::string& label() const { return name.empty() ? id : name; }
};

class GCalStore : public PersistableStore<GCalStore> {
 private:
  std::string clientId;
  std::string clientSecret;
  std::string refreshToken;
  // Calendars the user ticked. Empty means "not chosen yet"; the sync treats
  // that as nothing to fetch rather than silently pulling every calendar.
  std::vector<SelectedCalendar> selectedCalendars;

  GCalStore() = default;
  ~GCalStore() = default;

  friend class PersistableStore<GCalStore>;

 public:
  // Google client ids run ~72 chars and secrets ~35; refresh tokens are ~100 but
  // are not contractually bounded. The caps stop a corrupt file allocating
  // freely while leaving generous headroom.
  static constexpr size_t MAX_CLIENT_ID_LEN = 128;
  static constexpr size_t MAX_SECRET_LEN = 128;
  static constexpr size_t MAX_TOKEN_LEN = 512;
  // Calendar ids are email-shaped. A reader showing more than this many
  // calendars at once has stopped being readable.
  static constexpr size_t MAX_CALENDARS = 8;
  static constexpr size_t MAX_CALENDAR_ID_LEN = 128;
  // Long enough for any calendar name worth reading in a tab; a longer one is
  // truncated by the tab bar anyway.
  static constexpr size_t MAX_CALENDAR_NAME_LEN = 48;

  static const char* getFilePath() { return "/.crosspoint/gcal.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setClientId(const std::string& value);
  const std::string& getClientId() const { return clientId; }
  void setClientSecret(const std::string& value);
  const std::string& getClientSecret() const { return clientSecret; }
  // Both halves are needed before the device flow can start.
  bool hasClientCredentials() const { return !clientId.empty() && !clientSecret.empty(); }

  void setRefreshToken(const std::string& value);
  const std::string& getRefreshToken() const { return refreshToken; }
  bool isLinked() const { return !refreshToken.empty(); }
  // Drops the refresh token and the calendar selection, keeping the client
  // credentials so the account can be re-linked without retyping them.
  void unlink();

  const std::vector<SelectedCalendar>& getSelectedCalendars() const { return selectedCalendars; }
  bool isCalendarSelected(const std::string& id) const;
  // Adds or removes the id, capped at MAX_CALENDARS. No-op past the cap. The
  // name is recorded alongside on add, so the Calendar screen can label its tabs
  // without the radio; re-ticking a calendar refreshes a name that was missing.
  void toggleCalendar(const std::string& id, const std::string& name = {});
  void clearCalendars() { selectedCalendars.clear(); }
};

#define GCAL_STORE GCalStore::getInstance()
