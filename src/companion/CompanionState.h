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
  // Local day number the best-ever single-day tasks+habits total was last
  // beaten on. The companion reads as the Milestone mood for the rest of that
  // day (see CompanionTracker::currentMood()) rather than for one paint only,
  // so Home, the sleep screen, and anywhere else that asks all agree without
  // needing to coordinate over who gets to consume a one-shot flag; it just
  // stops matching once the day rolls past it, on its own.
  // companion::DayLedger::NEVER until a record has ever been beaten.
  int32_t milestoneDay = companion::DayLedger::NEVER;
  // Local day number the companion was first ever enabled, for the settings
  // screen's "active for" display. companion::DayLedger::NEVER until then.
  // Stamped once and never moved, even if the companion is later disabled and
  // re-enabled -- it answers "how long has this device had a companion", not
  // "how long has it been on right now".
  int32_t activatedDay = companion::DayLedger::NEVER;

  static const char* getFilePath() { return "/.crosspoint/companion.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Credits today's combined tasks+habits effort into the ledger. `localDay`
  // is ignored (no ledger update) when clockValid is false, since day
  // arithmetic would be meaningless without a real calendar day to key it to.
  // `thresholds` only matters for its satisfiedPoints -- the "qualifying day"
  // bar creditQualifyingDay() checks against -- so this stays in step with
  // whatever bar evaluate() is using for the same day; the caller is expected
  // to already have it clamped (see CompanionTracker::thresholdsFromSettings).
  // Returns true when something changed and the caller should persist.
  bool recordActivity(int32_t localDay, bool clockValid, uint16_t tasksCompletedToday, uint16_t habitsCompletedToday,
                      const companion::MoodThresholds& thresholds = {});
};

#define COMPANION_STATE CompanionState::getInstance()
