#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "GCalEvent.h"

/**
 * Singleton holding the last synced window of events.
 *
 * The Calendar screen renders straight from here, so opening it needs no Wi-Fi:
 * a sync is an explicit user action, and the cached list stays readable in
 * between. Events are read-only on this device; nothing is ever pushed back.
 */
class GCalEventCache : public PersistableStore<GCalEventCache> {
 private:
  std::vector<GCalEvent> events;          // Chronological, all-day first per day
  uint16_t syncDate = civil::NO_DATE;     // The "today" the window was built from

  GCalEventCache() = default;
  ~GCalEventCache() = default;

  friend class PersistableStore<GCalEventCache>;

 public:
  static constexpr size_t MAX_EVENTS = GCAL_MAX_EVENTS;

  static const char* getFilePath() { return "/.crosspoint/gcal_events.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<GCalEvent>& getEvents() const { return events; }
  uint16_t getSyncDate() const { return syncDate; }
  bool hasSynced() const { return syncDate != civil::NO_DATE; }

  // Number of events falling on the sync day itself, for the header count.
  size_t getTodayCount() const;

  /**
   * Replaces the list after a successful sync.
   *
   * Sorted chronologically, with all-day events leading their own day. Stable,
   * so several calendars merged into one vector keep a predictable order when
   * two events start at the same minute.
   *
   * `today` is the date the window was anchored on. NO_DATE leaves the stored
   * date untouched, so a sync that could not establish today keeps showing the
   * last date it did know.
   */
  void setEvents(std::vector<GCalEvent>&& fetched, uint16_t today);

  void clear();
};

#define GCAL_EVENTS GCalEventCache::getInstance()
