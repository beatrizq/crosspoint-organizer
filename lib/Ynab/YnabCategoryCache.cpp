#include "YnabCategoryCache.h"

#include <Logging.h>

#include <algorithm>

void YnabCategoryCache::toJson(JsonDocument& doc) const {
  char iso[11];
  civil::isoFromDate(syncMonth, iso, sizeof(iso));
  doc["month"] = iso;

  JsonArray arr = doc["categories"].to<JsonArray>();
  for (const auto& category : categories) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = category.name;
    obj["balance"] = category.balance;
  }
}

bool YnabCategoryCache::fromJson(JsonVariantConst doc) {
  categories.clear();
  syncMonth = civil::dateFromIso(doc["month"] | "");

  JsonArrayConst arr = doc["categories"].as<JsonArrayConst>();
  categories.reserve(std::min(arr.size(), MAX_CATEGORIES));
  for (JsonObjectConst obj : arr) {
    if (categories.size() >= MAX_CATEGORIES) break;
    YnabCategory category;
    category.name = obj["name"] | "";
    category.balance = obj["balance"] | "";
    if (category.name.empty()) continue;
    categories.push_back(std::move(category));
  }

  LOG_DBG("YCC", "Loaded %zu categories", categories.size());
  return true;
}

void YnabCategoryCache::setCategories(std::vector<YnabCategory>&& fetched, const uint16_t month) {
  categories = std::move(fetched);
  if (categories.size() > MAX_CATEGORIES) categories.resize(MAX_CATEGORIES);
  if (month != civil::NO_DATE) syncMonth = month;
}

void YnabCategoryCache::clear() {
  categories.clear();
  syncMonth = civil::NO_DATE;
}
