#include "CompanionWallpaperStore.h"

void CompanionWallpaperStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["pathForMood"].to<JsonArray>();
  for (const auto& path : pathForMood) arr.add(path);
}

bool CompanionWallpaperStore::fromJson(JsonVariantConst doc) {
  JsonArrayConst arr = doc["pathForMood"];
  for (size_t i = 0; i < companion::MOOD_COUNT; i++) {
    pathForMood[i].clear();
    if (!arr.isNull() && i < arr.size() && arr[i].is<const char*>()) {
      pathForMood[i] = arr[i].as<const char*>();
    }
  }
  return true;
}
