#pragma once
#include <CivilTime.h>

#include <cstddef>
#include <cstdint>

/**
 * Due-date helpers for the Todoist list.
 *
 * The calendar arithmetic lives in <CivilTime.h>, shared with the calendar
 * screen. This header keeps the todoist:: names the task code and its tests
 * already use, so the two callers cannot drift apart on how a date is packed.
 */
namespace todoist {

// Sentinel for "no due date". Chosen as the maximum so undated tasks sort after
// every real date and can never be mistaken for the newest one.
static constexpr uint16_t DUE_NONE = civil::NO_DATE;

// Days since 2000-01-01 for the leading "YYYY-MM-DD" of an ISO date or
// datetime; DUE_NONE for anything malformed, empty, or out of range.
constexpr uint16_t dueDaysFromIso(const char* iso) { return civil::dateFromIso(iso); }

// Renders a packed due date back to "YYYY-MM-DD". Writes an empty string for
// DUE_NONE. outSize must be at least 11.
inline void isoFromDueDays(const uint16_t due, char* out, const size_t outSize) {
  civil::isoFromDate(due, out, outSize);
}

}  // namespace todoist
