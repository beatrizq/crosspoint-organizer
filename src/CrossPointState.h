#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // Mirrors lastSleepFromReader for the companion's quick-pick reveal screen,
  // so waking from sleep can put the same pick back up instead of dropping to
  // Home. The pick's own content lives in the three fields below, kept in
  // sync by QuickPickActivity itself (written on entry) rather than fished
  // out reactively at sleep time -- there is no RTTI in this build to safely
  // downcast the current Activity and read it that way.
  bool lastSleepFromQuickPick = false;
  std::string quickPickText;
  // Todoist task id / Habitify habit id behind quickPickText, so resuming
  // this screen after sleep can still jump straight to that row.
  std::string quickPickItemId;
  bool quickPickIsHabit = false;
  bool quickPickPoolEmpty = false;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
};

// Helper macro to access state
#define APP_STATE CrossPointState::getInstance()
