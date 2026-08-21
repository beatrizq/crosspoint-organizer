#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * Singleton holding the Habitify API key on the SD card.
 *
 * Habitify issues one key at a time from its own app (Settings -> API), and
 * generating a new one revokes the previous, so there is no pairing flow to run
 * and nothing to refresh: the key is entered once and used as a credential on
 * every request. Note that it goes in an X-API-Key header rather than as a
 * bearer token, unlike every other integration here.
 *
 * The key is XOR-obfuscated with the device's hardware MAC and base64-encoded
 * before it is written, the same scheme as the Todoist, Google, YNAB, KOReader
 * and OPDS credentials: not cryptographically secure, but it keeps a long-lived
 * credential out of plain sight on a card that gets mounted on a PC, and it
 * cannot be decoded on another chip.
 */
class HabitifyStore : public PersistableStore<HabitifyStore> {
 private:
  std::string apiKey;
  // Drop a habit from the Today list once it has met its goal, so the list is
  // what is left to do rather than a checklist of what is done.
  bool hideCompleted = false;

  HabitifyStore() = default;
  ~HabitifyStore() = default;

  friend class PersistableStore<HabitifyStore>;

 public:
  // Habitify does not document a key length; the cap stops a corrupt file
  // allocating freely while leaving room for anything token-shaped.
  static constexpr size_t MAX_KEY_LEN = 128;

  static const char* getFilePath() { return "/.crosspoint/habitify.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setApiKey(const std::string& value);
  const std::string& getApiKey() const { return apiKey; }
  bool hasApiKey() const { return !apiKey.empty(); }
  void clearApiKey();

  void setHideCompleted(bool hide) { hideCompleted = hide; }
  bool getHideCompleted() const { return hideCompleted; }
};

#define HABITIFY_STORE HabitifyStore::getInstance()
