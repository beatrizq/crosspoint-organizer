#include "GCalEventCache.h"

#include <Logging.h>

#include <algorithm>

void GCalEventCache::toJson(JsonDocument& doc) const {
  char iso[11];
  civil::isoFromDate(syncDate, iso, sizeof(iso));
  doc["syncDate"] = iso;

  JsonArray arr = doc["events"].to<JsonArray>();
  for (const auto& event : events) {
    JsonObject obj = arr.add<JsonObject>();
    obj["summary"] = event.summary;
    // Dates and times are written in their readable forms rather than as packed
    // integers, so the file stays inspectable from a PC.
    char day[11];
    civil::isoFromDate(event.date, day, sizeof(day));
    obj["date"] = day;
    if (!event.isAllDay()) {
      obj["start"] = event.startMin;
      if (event.endMin != civil::NO_TIME) obj["end"] = event.endMin;
    }
  }
}

bool GCalEventCache::fromJson(JsonVariantConst doc) {
  events.clear();
  syncDate = civil::dateFromIso(doc["syncDate"] | "");

  JsonArrayConst arr = doc["events"].as<JsonArrayConst>();
  events.reserve(std::min(arr.size(), MAX_EVENTS));
  for (JsonObjectConst obj : arr) {
    if (events.size() >= MAX_EVENTS) break;
    GCalEvent event;
    event.summary = obj["summary"] | "";
    event.date = civil::dateFromIso(obj["date"] | "");
    // Absent "start" means an all-day event, which is how it was written.
    event.startMin = obj["start"] | civil::NO_TIME;
    event.endMin = obj["end"] | civil::NO_TIME;
    if (event.date == civil::NO_DATE) continue;
    events.push_back(std::move(event));
  }

  LOG_DBG("GEC", "Loaded %zu events", events.size());
  return true;
}

size_t GCalEventCache::getTodayCount() const {
  if (syncDate == civil::NO_DATE) return 0;
  return static_cast<size_t>(
      std::count_if(events.begin(), events.end(), [this](const GCalEvent& e) { return e.date == syncDate; }));
}

void GCalEventCache::setEvents(std::vector<GCalEvent>&& fetched, const uint16_t today) {
  events = std::move(fetched);
  // Sort before trimming, so the cap drops the furthest-out events rather than
  // whichever calendar happened to be fetched last.
  std::stable_sort(events.begin(), events.end(),
                   [](const GCalEvent& a, const GCalEvent& b) { return a.sortKey() < b.sortKey(); });
  if (events.size() > MAX_EVENTS) events.resize(MAX_EVENTS);
  if (today != civil::NO_DATE) syncDate = today;
}

void GCalEventCache::clear() {
  events.clear();
  syncDate = civil::NO_DATE;
}
