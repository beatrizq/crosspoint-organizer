#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

/**
 * Calendar helpers for the Todoist list.
 *
 * Due dates are held as days since 2000-01-01 in a uint16_t rather than as an
 * ISO string: at TODOIST_MAX_TASKS the packed form costs 2 bytes per task
 * (120 bytes for a full list) against ~11 bytes plus a separate heap block per
 * std::string, which matters on a fragmenting 380KB heap. The range runs to
 * year 2179, well past any due date a reader will hold.
 *
 * Header-only and free of Arduino/ESP dependencies so the host unit tests can
 * include it without linking the firmware.
 */
namespace todoist {

// Sentinel for "no due date". Chosen as the maximum so undated tasks sort after
// every real date and can never be mistaken for the newest one.
static constexpr uint16_t DUE_NONE = 0xFFFF;

// Days from 1970-01-01 to 2000-01-01, the epoch the packed form counts from.
static constexpr int32_t DAYS_1970_TO_2000 = 10957;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Valid for any y/m/d the callers below have range-checked.
constexpr int32_t daysFromCivil(int32_t y, const uint32_t m, const uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// Inverse of daysFromCivil (Hinnant's civil_from_days).
constexpr void civilFromDays(int32_t z, int32_t& outY, uint32_t& outM, uint32_t& outD) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  outD = doy - (153 * mp + 2) / 5 + 1;
  outM = mp + (mp < 10 ? 3 : -9);
  outY = y + (outM <= 2);
}

// Parses the leading "YYYY-MM-DD" of an ISO date or datetime. Returns DUE_NONE
// for anything malformed, empty, or outside the representable range, so a
// hand-edited cache file or an unexpected server format degrades to "undated"
// instead of producing a bogus ordering.
constexpr uint16_t dueDaysFromIso(const char* iso) {
  if (iso == nullptr) return DUE_NONE;
  // Only the date portion is read; Todoist sends either "YYYY-MM-DD" or a full
  // datetime, and both start with the same 10 characters.
  for (int i = 0; i < 10; i++) {
    if (iso[i] == '\0') return DUE_NONE;
  }
  if (iso[4] != '-' || iso[7] != '-') return DUE_NONE;
  // Digit positions of YYYY-MM-DD, skipping the two separators. Checked without
  // an initializer_list so the function stays dependency-free.
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) continue;
    if (iso[i] < '0' || iso[i] > '9') return DUE_NONE;
  }

  const int32_t year = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0');
  const uint32_t month = static_cast<uint32_t>((iso[5] - '0') * 10 + (iso[6] - '0'));
  const uint32_t day = static_cast<uint32_t>((iso[8] - '0') * 10 + (iso[9] - '0'));
  if (month < 1 || month > 12 || day < 1 || day > 31) return DUE_NONE;

  const int32_t days = daysFromCivil(year, month, day) - DAYS_1970_TO_2000;
  // DUE_NONE is reserved, so the last representable day is DUE_NONE - 1.
  if (days < 0 || days >= static_cast<int32_t>(DUE_NONE)) return DUE_NONE;
  return static_cast<uint16_t>(days);
}

// Renders a packed due date back to "YYYY-MM-DD". Writes an empty string for
// DUE_NONE. outSize must be at least 11.
inline void isoFromDueDays(const uint16_t due, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  if (due == DUE_NONE || outSize < 11) {
    out[0] = '\0';
    return;
  }
  int32_t y = 0;
  uint32_t m = 0;
  uint32_t d = 0;
  civilFromDays(static_cast<int32_t>(due) + DAYS_1970_TO_2000, y, m, d);
  snprintf(out, outSize, "%04d-%02u-%02u", static_cast<int>(y), static_cast<unsigned>(m), static_cast<unsigned>(d));
}

}  // namespace todoist
