#pragma once

#include <Arduino.h>
#include <Rtc.h>

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // Get current hour (0-23) and minute (0-59), for the status bar clock.
  // Reads the hardware RTC when present (cached briefly to limit I2C
  // traffic); on boards without one (X3/X4 included) falls back to the
  // system clock, the same source and validity check getUtcDateTime() uses.
  // Returns false if neither has a usable time.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Full UTC wall-clock reading, including the calendar date.
  //
  // Unlike getTime() this never substitutes a cached value: callers doing date
  // arithmetic (streaks, the organizing companion's mood) must be able to
  // tell "clock unknown" from a real date, because a stale one silently
  // corrupts elapsed-day maths. Reads the hardware RTC when one is present;
  // most boards (X3/X4 included) have none, so this falls back to the system
  // clock once it has actually been set via SNTP, and returns false until
  // then. See the .cpp for why that fallback does not use SNTP's own
  // sync-status flag.
  //
  // Performs an I2C transaction on every call when a hardware RTC is present,
  // so keep it off render paths.
  bool getUtcDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const;

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false if RTC is not available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Same formatting/offset math as formatTime(), for an arbitrary UTC
  // hour/minute rather than the current reading -- for a caller displaying a
  // computed time (a countdown's end time) rather than "now". Same buffer
  // sizing and offset convention as formatTime(); does not touch the RTC, so
  // it needs no instance. Returns false (buf untouched) only if bufSize is
  // too small.
  static bool formatHourMinute(char* buf, size_t bufSize, uint8_t hour, uint8_t minute,
                               uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false);

  // Sync the RTC from an NTP server. Requires WiFi to be connected.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true if the RTC was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
