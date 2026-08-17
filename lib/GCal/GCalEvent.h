#pragma once
#include <CivilTime.h>

#include <string>

/**
 * One row of the Calendar screen.
 *
 * Descriptions, locations, attendees, organisers and colours are dropped at
 * parse time; only what the list draws survives. Start and end are packed into
 * 2 bytes each rather than kept as RFC3339 strings, so a full window costs
 * ~6 bytes plus the summary per event instead of three heap blocks.
 */
struct GCalEvent {
  std::string summary;                     // Event title, truncated at parse time
  uint16_t date = civil::NO_DATE;          // Start day, days since 2000-01-01
  uint16_t startMin = civil::NO_TIME;      // Minutes since midnight; NO_TIME = all-day
  uint16_t endMin = civil::NO_TIME;        // Minutes since midnight; NO_TIME if unknown

  // Google returns all-day events with a plain "date" and timed events with a
  // "dateTime", so the absence of a time is what distinguishes them.
  bool isAllDay() const { return startMin == civil::NO_TIME; }

  // Sort key: chronological, with all-day events leading their own day. NO_TIME
  // is the maximum, so it is folded to 0 to put all-day entries first.
  uint32_t sortKey() const {
    const uint32_t day = static_cast<uint32_t>(date);
    const uint32_t minute = startMin == civil::NO_TIME ? 0u : static_cast<uint32_t>(startMin);
    return day * 1440u + minute;
  }

  static constexpr size_t SUMMARY_MAX_LEN = 64;
};

// A month of events for a busy calendar, bounded so a runaway response cannot
// exhaust the heap. Beyond this the list is longer than anyone will scroll.
static constexpr size_t GCAL_MAX_EVENTS = 80;

// Days of events the Calendar screen requests, counting today as day one.
static constexpr uint16_t GCAL_WINDOW_DAYS = 30;
