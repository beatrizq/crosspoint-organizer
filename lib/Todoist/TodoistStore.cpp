#include "TodoistStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

void TodoistStore::toJson(JsonDocument& doc) const {
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
  doc["sleepScreen"] = sleepScreenEnabled;
}

bool TodoistStore::fromJson(JsonVariantConst doc) {
  token.clear();
  sleepScreenEnabled = doc["sleepScreen"] | true;

  const char* obfuscated = doc["token_obf"] | "";
  if (obfuscated[0] != '\0') {
    bool ok = false;
    bool tooLong = false;
    token = obfuscation::deobfuscateFromBase64(obfuscated, MAX_TOKEN_LEN, &ok, &tooLong);
    if (!ok) {
      // Wrong device (the key is the MAC) or a corrupt file: drop it rather
      // than sending garbage to the API on every sync.
      LOG_ERR("TDS", "Token failed to decode (%s), clearing", tooLong ? "too long" : "bad base64");
      token.clear();
    }
    return true;
  }

  // Plaintext fallback so the token can be dropped into the JSON by hand from a
  // PC; it is re-saved obfuscated on load.
  const char* plain = doc["token"] | "";
  if (plain[0] != '\0' && strlen(plain) <= MAX_TOKEN_LEN) {
    token = plain;
    LOG_DBG("TDS", "Plaintext token found, resaving obfuscated");
    requestResave();
  }
  return true;
}

void TodoistStore::setToken(const std::string& value) {
  token = value.size() > MAX_TOKEN_LEN ? value.substr(0, MAX_TOKEN_LEN) : value;
}

void TodoistStore::clearToken() { token.clear(); }
