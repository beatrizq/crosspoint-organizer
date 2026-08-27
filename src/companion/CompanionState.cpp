#include "CompanionState.h"

void CompanionState::toJson(JsonDocument& doc) const {
  doc["lastQualifyingDay"] = ledger.lastQualifyingDay;
  doc["bestDayPoints"] = ledger.bestDayPoints;
  doc["milestoneDay"] = milestoneDay;
  doc["activatedDay"] = activatedDay;
}

bool CompanionState::fromJson(JsonVariantConst doc) {
  ledger.lastQualifyingDay = doc["lastQualifyingDay"] | companion::DayLedger::NEVER;
  ledger.bestDayPoints = doc["bestDayPoints"] | static_cast<uint16_t>(0);
  milestoneDay = doc["milestoneDay"] | companion::DayLedger::NEVER;
  activatedDay = doc["activatedDay"] | companion::DayLedger::NEVER;
  return true;
}

bool CompanionState::recordActivity(const int32_t localDay, const bool clockValid, const uint16_t tasksCompletedToday,
                                    const uint16_t habitsCompletedToday, const companion::MoodThresholds& thresholds) {
  if (!clockValid) return false;

  const uint16_t bestBefore = ledger.bestDayPoints;
  const bool changed =
      companion::creditQualifyingDay(ledger, localDay, tasksCompletedToday, habitsCompletedToday, thresholds);
  if (!changed) return false;
  // A first-ever recorded day sets the record up from zero, which is not an
  // achievement worth interrupting for; only a genuine improvement on an
  // existing record earns the milestone mood.
  if (bestBefore > 0 && ledger.bestDayPoints > bestBefore) milestoneDay = localDay;
  return true;
}

void CompanionState::reset() {
  ledger = companion::DayLedger{};
  milestoneDay = companion::DayLedger::NEVER;
  activatedDay = companion::DayLedger::NEVER;
}
