#pragma once
#include <ArduinoJson.h>
#include <CivilTime.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "HabitifyHabit.h"

/**
 * Singleton holding the last synced journal plus the progress added on the device
 * and not yet pushed.
 *
 * The Habits screen renders straight from here, so opening it needs no Wi-Fi: a
 * sync is an explicit user action (hold Select), and pressing Complete only
 * updates this cache. Pending progress is pushed on the next sync, before the
 * journal is re-fetched, so the fresh figures already include it - the same
 * arrangement as the Todoist completion queue.
 *
 * `syncDate` is the day the journal was fetched for. It matters more here than on
 * the other screens: progress is per-day, so a cache from yesterday shows
 * yesterday's counts, and the screen says which day it is holding.
 */
class HabitifyHabitCache : public PersistableStore<HabitifyHabitCache> {
 private:
  std::vector<HabitifyHabit> habits;
  uint16_t syncDate = civil::NO_DATE;

  HabitifyHabitCache() = default;
  ~HabitifyHabitCache() = default;

  friend class PersistableStore<HabitifyHabitCache>;

 public:
  static constexpr size_t MAX_HABITS = HABITIFY_MAX_HABITS;

  static const char* getFilePath() { return "/.crosspoint/habitify_habits.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<HabitifyHabit>& getHabits() const { return habits; }
  uint16_t getSyncDate() const { return syncDate; }
  bool hasSynced() const { return syncDate != civil::NO_DATE; }

  /**
   * Replaces the list after a successful fetch, keeping the habit order the API
   * returned - which is the order Habitify itself shows.
   *
   * Pending progress is carried across by id for any habit that survives the
   * fetch. It has to be: the push and the re-fetch are separate requests, and a
   * press that landed between them would otherwise be silently dropped. Progress
   * already pushed is cleared by clearPending() before this is called, so what
   * carries over is only what is still owed.
   *
   * `date` is the day the journal was fetched for. NO_DATE leaves the stored date
   * alone, so a sync whose response carried no date keeps the last one that did.
   */
  void setHabits(std::vector<HabitifyHabit>&& fetched, uint16_t date);

  // Adds to a habit's unpushed progress. No-op for an unknown index.
  void addPending(size_t index, float amount);
  // Drops what was successfully pushed for one habit. Subtracted rather than
  // zeroed: a press that arrived while the request was in flight is still owed.
  void clearPending(const std::string& habitId, float pushed);
  bool hasPending() const;
  // Habits carrying unpushed progress, for the header's count.
  size_t pendingCount() const;

  // Marks the habit complete immediately (completedByStatus set optimistically,
  // so the row reads done and the companion can credit it right away) and
  // queues a complete push for the next sync. No-op for an unknown index.
  void completeHabitAt(size_t index);
  // Called once the server accepted a queued complete, or the habit it was for
  // turned out to already be gone.
  void clearPendingComplete(const std::string& habitId);

  void clear();
};

#define HABITIFY_HABITS HabitifyHabitCache::getInstance()
