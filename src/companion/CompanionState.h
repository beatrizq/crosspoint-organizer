#pragma once
#include <ArduinoJson.h>
#include <CompanionMood.h>
#include <PersistableStore.h>

#include <cstdint>

/**
 * @brief Persisted companion progress: the day ledger plus lifetime totals.
 *
 * Only earned data lives here. Which companion is active and whether the
 * feature is on at all are user preferences and live in CrossPointSettings,
 * mirroring the CrossPointSettings/CrossPointState split.
 *
 * All day-rollover and streak logic is in lib/Companion so it stays host
 * testable; this class is the JSON envelope around it.
 */
class CompanionState : public PersistableStore<CompanionState> {
  CompanionState() = default;

  friend class PersistableStore<CompanionState>;

 public:
  companion::DayLedger ledger;
  // Set when a day's activity pushes the streak past its previous best,
  // cleared once the companion has actually said something about it.
  // Persisted so the moment survives the sleep between earning it and next
  // opening Home, which is exactly when it is most likely to be earned.
  bool milestonePending = false;

  static const char* getFilePath() { return "/.crosspoint/companion.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Credits today's combined tasks+habits effort into the ledger. `localDay`
  // is ignored (no streak update) when clockValid is false, since day
  // arithmetic would be meaningless without a real calendar day to key it to.
  // Returns true when something changed and the caller should persist.
  bool recordActivity(int32_t localDay, bool clockValid, uint16_t tasksCompletedToday, uint16_t habitsCompletedToday);
};

#define COMPANION_STATE CompanionState::getInstance()
