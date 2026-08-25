#include "CompanionState.h"

void CompanionState::toJson(JsonDocument& doc) const {
  doc["lastQualifyingDay"] = ledger.lastQualifyingDay;
  doc["streakDays"] = ledger.streakDays;
  doc["bestStreakDays"] = ledger.bestStreakDays;
  doc["milestonePending"] = milestonePending;
  doc["activatedDay"] = activatedDay;
}

bool CompanionState::fromJson(JsonVariantConst doc) {
  ledger.lastQualifyingDay = doc["lastQualifyingDay"] | companion::DayLedger::NEVER;
  ledger.streakDays = doc["streakDays"] | static_cast<uint16_t>(0);
  ledger.bestStreakDays = doc["bestStreakDays"] | static_cast<uint16_t>(0);
  milestonePending = doc["milestonePending"] | false;
  activatedDay = doc["activatedDay"] | companion::DayLedger::NEVER;

  // A hand-edited or truncated file must not leave best below current.
  if (ledger.bestStreakDays < ledger.streakDays) ledger.bestStreakDays = ledger.streakDays;
  return true;
}

bool CompanionState::recordActivity(const int32_t localDay, const bool clockValid, const uint16_t tasksCompletedToday,
                                    const uint16_t habitsCompletedToday) {
  if (!clockValid) return false;

  const uint16_t bestBefore = ledger.bestStreakDays;
  if (!companion::creditQualifyingDay(ledger, localDay, tasksCompletedToday, habitsCompletedToday)) return false;
  // A first-ever qualifying day sets best to 1, which is not an achievement
  // worth interrupting for; only a genuine improvement on an existing record
  // earns the milestone line.
  if (bestBefore > 1 && ledger.bestStreakDays > bestBefore) milestonePending = true;
  return true;
}
