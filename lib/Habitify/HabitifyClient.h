#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "HabitifyHabit.h"

/**
 * HTTPS client for the Habitify API v2.
 *
 * Base URL: https://api.habitify.me/v2
 *
 * Endpoints used:
 *   GET  /habits/journal            - habits with today's progress, for the screen
 *   POST /habits/{id}/logs          - add progress to a habit
 *   POST /habits/{id}/logs/complete - mark a habit complete directly, no amount needed
 *
 * Authentication: an `X-API-Key` header carrying the key generated in Habitify's
 * own app (Settings -> API). Deliberately noted, because it is unlike every other
 * integration here: Todoist, Google and YNAB all take `Authorization: Bearer`,
 * and sending this key that way returns 401. Habitify keeps one key active at a
 * time, so generating a new one in the app invalidates whatever is on the device.
 *
 * API access needs a paid Habitify plan (their Pro tier); a free account's key
 * is rejected rather than returning an empty journal.
 *
 * Habitify allows 500 requests per minute per account, which is generous enough
 * that this screen never has to ration them the way the YNAB one does.
 */
class HabitifyClient {
 public:
  enum Error {
    OK = 0,
    NO_KEY,
    NETWORK_ERROR,
    AUTH_FAILED,   // 401/403: the key was rejected, or the plan does not include API access
    NOT_FOUND,     // 404: no such habit
    RATE_LIMITED,  // 429: 500 requests per minute per account
    SERVER_ERROR,
    PARSE_ERROR,
    LOW_MEMORY,
  };

  /**
   * Fetches the journal - every habit with its progress for a date.
   *
   * @param outHabits Output: parsed habits in the API's own order, capped at
   *                  HABITIFY_MAX_HABITS. `pending` is left at zero; carrying it
   *                  across a refetch is the cache's job.
   * @param outDate   Output: the day the journal is for, taken from the
   *                  response's HTTP Date header, or civil::NO_DATE when the
   *                  header was missing. The request asks for the account's own
   *                  today rather than naming a date, so this is what the server
   *                  answered for - which matters on boards with no RTC.
   */
  static Error fetchJournal(std::vector<HabitifyHabit>& outHabits, uint16_t& outDate);

  /**
   * Adds progress to a habit: POST /habits/{id}/logs with {unitSymbol, value}.
   *
   * One request carries the whole accumulated amount, so three presses of
   * Complete cost one call rather than three - progress is a running total, so a
   * single log of 3 lands the same as three logs of 1.
   *
   * `unitSymbol` must be one of the API's units; it comes from the habit's own
   * progress.unit. A habit with no goal reports no unit, and cannot be logged
   * against - the caller checks that before getting here.
   */
  static Error addLog(const std::string& habitId, const std::string& unitSymbol, float value);

  /**
   * Marks a habit complete for today directly: POST /habits/{id}/logs/complete,
   * no body. Unlike addLog(), this needs no unit or amount, so it works for a
   * goal-less habit too - the one case addLog() can never touch at all. No date
   * is sent; the endpoint defaults to today, the same "let the server decide"
   * convention fetchJournal() already uses.
   */
  static Error completeHabit(const std::string& habitId);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
