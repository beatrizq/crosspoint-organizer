#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

#include "CompanionSprites.generated.h"

/**
 * One SD-card image path per companion mood, for the sleep screen -- see
 * SleepActivity's own mood-wallpaper check. Empty means "no wallpaper
 * assigned for this mood", falling back to the existing random-pool custom
 * sleep screen.
 *
 * Kept separate from CrossPointSettings rather than five more char[] fields
 * there: these are full SD paths (unbounded length, picked via the file
 * browser), not the short fixed-size strings that struct's SettingInfo
 * reflection is built around -- the same reason RecentBooksStore and
 * CompanionState live in their own files instead.
 */
class CompanionWallpaperStore : public PersistableStore<CompanionWallpaperStore> {
  CompanionWallpaperStore() = default;
  ~CompanionWallpaperStore() = default;

  friend class PersistableStore<CompanionWallpaperStore>;

 public:
  // Indexed by companion::Mood -- pathForMood[static_cast<size_t>(mood)].
  std::string pathForMood[companion::MOOD_COUNT];

  static const char* getFilePath() { return "/.crosspoint/companion_wallpapers.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define COMPANION_WALLPAPERS CompanionWallpaperStore::getInstance()
