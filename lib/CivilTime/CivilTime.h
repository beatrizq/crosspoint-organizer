#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

/**
 * Calendar arithmetic shared by the task and calendar screens.
 *
 * Dates are days since 2000-01-01 in a uint16_t and times are minutes since
 * midnight in a uint16_t. Packing matters here: an event list holds dozens of
 * entries, and two 2-byte fields beat two std::strings (~11 bytes plus a heap
 * block each) on a fragmenting 380KB heap. The date range runs to 2179.
 *
 * Header-only and free of Arduino/ESP dependencies so host unit tests can
 * include it without linking the firmware.
 */
namespace civil {

// "No date" / "no time". Both are the maximum so undated entries sort last and
// can never be mistaken for the newest value.
static constexpr uint16_t NO_DATE = 0xFFFF;
static constexpr uint16_t NO_TIME = 0xFFFF;

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

constexpr bool isDigit(const char c) { return c >= '0' && c <= '9'; }

// Packs an already-validated y/m/d, returning NO_DATE when out of range.
constexpr uint16_t packDate(const int32_t year, const uint32_t month, const uint32_t day) {
  if (month < 1 || month > 12 || day < 1 || day > 31) return NO_DATE;
  const int32_t days = daysFromCivil(year, month, day) - DAYS_1970_TO_2000;
  // NO_DATE is reserved, so the last representable day is NO_DATE - 1.
  if (days < 0 || days >= static_cast<int32_t>(NO_DATE)) return NO_DATE;
  return static_cast<uint16_t>(days);
}

// Parses the leading "YYYY-MM-DD" of an ISO date or datetime. Returns NO_DATE
// for anything malformed, empty, or outside the representable range.
constexpr uint16_t dateFromIso(const char* iso) {
  if (iso == nullptr) return NO_DATE;
  // Only the date portion is read; a full datetime starts with the same 10
  // characters.
  for (int i = 0; i < 10; i++) {
    if (iso[i] == '\0') return NO_DATE;
  }
  if (iso[4] != '-' || iso[7] != '-') return NO_DATE;
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) continue;
    if (!isDigit(iso[i])) return NO_DATE;
  }
  const int32_t year = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0');
  const uint32_t month = static_cast<uint32_t>((iso[5] - '0') * 10 + (iso[6] - '0'));
  const uint32_t day = static_cast<uint32_t>((iso[8] - '0') * 10 + (iso[9] - '0'));
  return packDate(year, month, day);
}

// Parses the "HH:MM" that follows the 'T' of an RFC3339 datetime, as minutes
// since midnight. Returns NO_TIME when the string is a plain date (no 'T') or
// the time is malformed.
//
// The wall-clock reading is taken verbatim and the UTC offset is ignored on
// purpose: Google returns each event's start in its own calendar's timezone, so
// the literal digits are already the time to put on screen. Converting through
// UTC would need a timezone database the device does not carry.
constexpr uint16_t timeFromRfc3339(const char* iso) {
  if (iso == nullptr) return NO_TIME;
  for (int i = 0; i < 10; i++) {
    if (iso[i] == '\0') return NO_TIME;
  }
  if (iso[10] != 'T' && iso[10] != 't' && iso[10] != ' ') return NO_TIME;
  if (!isDigit(iso[11]) || !isDigit(iso[12]) || iso[13] != ':' || !isDigit(iso[14]) || !isDigit(iso[15])) {
    return NO_TIME;
  }
  const uint32_t hour = static_cast<uint32_t>((iso[11] - '0') * 10 + (iso[12] - '0'));
  const uint32_t minute = static_cast<uint32_t>((iso[14] - '0') * 10 + (iso[15] - '0'));
  if (hour > 23 || minute > 59) return NO_TIME;
  return static_cast<uint16_t>(hour * 60 + minute);
}

// Renders a packed date as "YYYY-MM-DD". Writes an empty string for NO_DATE.
// outSize must be at least 11.
inline void isoFromDate(const uint16_t date, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  if (date == NO_DATE || outSize < 11) {
    out[0] = '\0';
    return;
  }
  int32_t y = 0;
  uint32_t m = 0;
  uint32_t d = 0;
  civilFromDays(static_cast<int32_t>(date) + DAYS_1970_TO_2000, y, m, d);
  snprintf(out, outSize, "%04d-%02u-%02u", static_cast<int>(y), static_cast<unsigned>(m), static_cast<unsigned>(d));
}

// Renders a packed date as an RFC3339 instant at midnight UTC, the form the
// Google Calendar API wants for timeMin/timeMax. outSize must be at least 21.
inline void rfc3339FromDate(const uint16_t date, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  if (date == NO_DATE || outSize < 21) {
    out[0] = '\0';
    return;
  }
  char iso[11];
  isoFromDate(date, iso, sizeof(iso));
  snprintf(out, outSize, "%sT00:00:00Z", iso);
}

// Day of week for a packed date: 0 = Sunday. 1970-01-01 was a Thursday.
constexpr uint8_t weekdayFromDate(const uint16_t date) {
  if (date == NO_DATE) return 0;
  const int32_t days1970 = static_cast<int32_t>(date) + DAYS_1970_TO_2000;
  return static_cast<uint8_t>((days1970 + 4) % 7);
}

// Month index (1-12) for a packed date, 0 when undated. Callers map this to a
// translated month name; the numeric form keeps this header free of i18n.
constexpr uint8_t monthFromDate(const uint16_t date) {
  if (date == NO_DATE) return 0;
  int32_t y = 0;
  uint32_t m = 0;
  uint32_t d = 0;
  civilFromDays(static_cast<int32_t>(date) + DAYS_1970_TO_2000, y, m, d);
  return static_cast<uint8_t>(m);
}

// Day of month (1-31) for a packed date, 0 when undated.
constexpr uint8_t dayOfMonthFromDate(const uint16_t date) {
  if (date == NO_DATE) return 0;
  int32_t y = 0;
  uint32_t m = 0;
  uint32_t d = 0;
  civilFromDays(static_cast<int32_t>(date) + DAYS_1970_TO_2000, y, m, d);
  return static_cast<uint8_t>(d);
}

// Parses the date out of an HTTP "Date:" response header, whose format is fixed
// by RFC 7231: "Sun, 17 Aug 2026 16:47:35 GMT".
//
// This is the device's most dependable clock. It has no RTC on most boards, and
// SNTP can be blocked or slow on a given network, but a response header cannot
// fail when the request it came with succeeded.
inline uint16_t dateFromHttpHeader(const char* header) {
  if (header == nullptr) return NO_DATE;
  // Skip the leading weekday and comma ("Sun, "), which is fixed-width but not
  // worth trusting; scan to the first digit instead.
  const char* p = header;
  while (*p != '\0' && !isDigit(*p)) p++;
  if (!isDigit(p[0]) || !isDigit(p[1]) || p[2] != ' ') return NO_DATE;
  const uint32_t day = static_cast<uint32_t>((p[0] - '0') * 10 + (p[1] - '0'));

  static constexpr char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* mon = p + 3;
  if (mon[0] == '\0' || mon[1] == '\0' || mon[2] == '\0') return NO_DATE;
  uint32_t month = 0;
  for (uint32_t i = 0; i < 12; i++) {
    if (MONTHS[i * 3] == mon[0] && MONTHS[i * 3 + 1] == mon[1] && MONTHS[i * 3 + 2] == mon[2]) {
      month = i + 1;
      break;
    }
  }
  if (month == 0) return NO_DATE;

  const char* yr = mon + 4;
  if (!isDigit(yr[0]) || !isDigit(yr[1]) || !isDigit(yr[2]) || !isDigit(yr[3])) return NO_DATE;
  const int32_t year = (yr[0] - '0') * 1000 + (yr[1] - '0') * 100 + (yr[2] - '0') * 10 + (yr[3] - '0');
  return packDate(year, month, day);
}

}  // namespace civil
