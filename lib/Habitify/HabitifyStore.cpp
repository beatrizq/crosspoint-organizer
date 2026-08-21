#include "HabitifyStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

void HabitifyStore::toJson(JsonDocument& doc) const {
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(apiKey);
  doc["hideCompleted"] = hideCompleted;
}

bool HabitifyStore::fromJson(JsonVariantConst doc) {
  apiKey.clear();
  hideCompleted = doc["hideCompleted"] | false;

  const char* obfuscated = doc["apiKey_obf"] | "";
  if (obfuscated[0] != '\0') {
    bool ok = false;
    bool tooLong = false;
    apiKey = obfuscation::deobfuscateFromBase64(obfuscated, MAX_KEY_LEN, &ok, &tooLong);
    if (!ok) {
      // Wrong device (the key is the MAC) or a corrupt file: drop it rather than
      // sending garbage to the API on every sync.
      LOG_ERR("HBS", "API key failed to decode (%s), clearing", tooLong ? "too long" : "bad base64");
      apiKey.clear();
    }
    return true;
  }

  // Plaintext fallback so the key can be dropped into the JSON by hand from a
  // PC; it is re-saved obfuscated on load, which rewrites the file without the
  // plaintext field.
  const char* plain = doc["apiKey"] | "";
  if (plain[0] == '\0') return true;
  if (strlen(plain) > MAX_KEY_LEN) {
    // Said out loud rather than skipped silently: hand-editing the file is the
    // main way this key gets set, and a key dropped for length would otherwise
    // look exactly like a key that was never read.
    LOG_ERR("HBS", "Plaintext API key is %u chars, over the %u cap; ignored", static_cast<unsigned>(strlen(plain)),
            static_cast<unsigned>(MAX_KEY_LEN));
    return true;
  }
  apiKey = plain;
  LOG_DBG("HBS", "Plaintext API key found, resaving obfuscated");
  requestResave();
  return true;
}

void HabitifyStore::setApiKey(const std::string& value) {
  apiKey = value.size() > MAX_KEY_LEN ? value.substr(0, MAX_KEY_LEN) : value;
}

void HabitifyStore::clearApiKey() { apiKey.clear(); }
