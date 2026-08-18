#include "YnabStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {
std::string clamped(const std::string& value, const size_t maxLen) {
  return value.size() > maxLen ? value.substr(0, maxLen) : value;
}
}  // namespace

void YnabStore::toJson(JsonDocument& doc) const {
  doc["accessToken_obf"] = obfuscation::obfuscateToBase64(accessToken);
  doc["budgetId"] = budgetId;

  JsonArray cats = doc["categories"].to<JsonArray>();
  for (const auto& id : selectedCategories) {
    cats.add(id);
  }
}

bool YnabStore::fromJson(JsonVariantConst doc) {
  accessToken.clear();
  budgetId.clear();
  selectedCategories.clear();

  budgetId = clamped(doc["budgetId"] | "", MAX_BUDGET_ID_LEN);

  bool needsResave = false;
  const char* obfuscated = doc["accessToken_obf"] | "";
  if (obfuscated[0] != '\0') {
    bool ok = false;
    bool tooLong = false;
    accessToken = obfuscation::deobfuscateFromBase64(obfuscated, MAX_TOKEN_LEN, &ok, &tooLong);
    if (!ok) {
      // Wrong device (the key is the MAC) or a corrupt file: drop it rather
      // than sending garbage to YNAB, which would burn the hourly request
      // budget on guaranteed 401s.
      LOG_ERR("YNS", "Access token failed to decode (%s), clearing", tooLong ? "too long" : "bad base64");
      accessToken.clear();
    }
  } else {
    // Plaintext fallback so the token can be dropped into the JSON by hand from
    // a PC; it is re-saved obfuscated on load.
    const char* plain = doc["accessToken"] | "";
    if (plain[0] != '\0') {
      accessToken = clamped(plain, MAX_TOKEN_LEN);
      needsResave = true;
    }
  }
  if (needsResave) {
    LOG_DBG("YNS", "Plaintext access token found, resaving obfuscated");
    requestResave();
  }

  JsonArrayConst cats = doc["categories"].as<JsonArrayConst>();
  selectedCategories.reserve(std::min(cats.size(), MAX_CATEGORIES));
  for (JsonVariantConst value : cats) {
    if (selectedCategories.size() >= MAX_CATEGORIES) break;
    const char* id = value | "";
    if (id[0] == '\0') continue;
    selectedCategories.emplace_back(clamped(id, MAX_CATEGORY_ID_LEN));
  }

  LOG_DBG("YNS", "Loaded: token %s, budget %s, %zu categories", accessToken.empty() ? "no" : "yes",
          budgetId.empty() ? "no" : "yes", selectedCategories.size());
  return true;
}

void YnabStore::setAccessToken(const std::string& value) { accessToken = clamped(value, MAX_TOKEN_LEN); }

void YnabStore::setBudgetId(const std::string& value) { budgetId = clamped(value, MAX_BUDGET_ID_LEN); }

void YnabStore::clearToken() {
  accessToken.clear();
  selectedCategories.clear();
}

bool YnabStore::isCategorySelected(const std::string& id) const {
  return std::find(selectedCategories.begin(), selectedCategories.end(), id) != selectedCategories.end();
}

void YnabStore::toggleCategory(const std::string& id) {
  const auto found = std::find(selectedCategories.begin(), selectedCategories.end(), id);
  if (found != selectedCategories.end()) {
    selectedCategories.erase(found);
    return;
  }
  if (selectedCategories.size() >= MAX_CATEGORIES) {
    LOG_ERR("YNS", "Category selection full (%zu), ignoring %s", MAX_CATEGORIES, id.c_str());
    return;
  }
  selectedCategories.emplace_back(clamped(id, MAX_CATEGORY_ID_LEN));
}
