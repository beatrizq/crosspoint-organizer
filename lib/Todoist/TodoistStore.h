#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * Singleton holding the Todoist API token on the SD card.
 *
 * The token is XOR-obfuscated with the device's hardware MAC and base64-encoded
 * before it is written (same scheme as the KOReader and OPDS credentials): not
 * cryptographically secure, but it keeps the token out of plain sight on a card
 * that is routinely mounted on a PC, and it cannot be decoded on another chip.
 */
class TodoistStore : public PersistableStore<TodoistStore> {
 private:
  std::string token;
  // Repaint /sleep.bmp from the Today screen after each successful sync, so the
  // sleeping device shows the task list.
  bool sleepScreenEnabled = true;

  TodoistStore() = default;
  ~TodoistStore() = default;

  friend class PersistableStore<TodoistStore>;

 public:
  // Personal API tokens are 40 hex characters; the cap leaves room for the
  // longer OAuth access tokens without letting a corrupt file allocate freely.
  static constexpr size_t MAX_TOKEN_LEN = 128;

  static const char* getFilePath() { return "/.crosspoint/todoist.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setToken(const std::string& value);
  const std::string& getToken() const { return token; }
  bool hasToken() const { return !token.empty(); }
  void clearToken();

  void setSleepScreenEnabled(bool enabled) { sleepScreenEnabled = enabled; }
  bool getSleepScreenEnabled() const { return sleepScreenEnabled; }
};

#define TODOIST_STORE TodoistStore::getInstance()
