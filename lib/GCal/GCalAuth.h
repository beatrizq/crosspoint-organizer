#pragma once
#include <cstdint>
#include <string>

/**
 * Google OAuth 2.0 device authorization grant (RFC 8628).
 *
 * The reader has no browser and no usable keyboard, so the interactive consent
 * screen is impossible here. The device flow moves that step to a phone: the
 * device asks Google for a short user code, shows it alongside a URL, and polls
 * until the user approves on another machine. The result is a refresh token that
 * survives reboots, which is the only credential kept on the card.
 *
 * Scope is calendar.readonly. The firmware never writes to a calendar, and a
 * read-only grant limits what a lost device can do.
 */
class GCalAuth {
 public:
  enum Error {
    OK = 0,
    NO_CLIENT,       // Client id/secret not configured yet
    NETWORK_ERROR,   // Transport failure or non-JSON reply
    AUTH_PENDING,    // User has not approved yet; keep polling
    SLOW_DOWN,       // Polling too fast; back off and keep polling
    ACCESS_DENIED,   // User refused
    CODE_EXPIRED,    // The user code timed out before approval
    INVALID_GRANT,   // Refresh token rejected: the account must be re-linked
    PARSE_ERROR,
    LOW_MEMORY,
  };

  // What the user needs to see to approve the device.
  struct DeviceCode {
    std::string deviceCode;       // Secret, polled with; never shown
    std::string userCode;         // Short code the user types, e.g. "ABCD-EFGH"
    std::string verificationUrl;  // Where they type it
    uint32_t intervalSec = 5;     // Minimum seconds between polls
    uint32_t expiresInSec = 1800;
  };

  /** Step 1: ask Google for a user code. Requires client credentials. */
  static Error requestDeviceCode(DeviceCode& out);

  /**
   * Step 2: exchange the device code for tokens. Returns AUTH_PENDING while the
   * user has not approved yet, so the caller polls this on `intervalSec`.
   * On OK, outRefreshToken holds the durable credential to persist.
   */
  static Error pollForTokens(const std::string& deviceCode, std::string& outRefreshToken, std::string& outAccessToken);

  /**
   * Mints a short-lived access token from the stored refresh token.
   *
   * outServerDate receives the date parsed from the response's HTTP `Date:`
   * header, packed as days since 2000-01-01, or civil::NO_DATE if absent. This
   * is the device's clock: most boards have no RTC and SNTP can be blocked on a
   * given network, but this header cannot fail when the request succeeded, and
   * every sync begins with this call anyway.
   */
  static Error refreshAccessToken(std::string& outAccessToken, uint16_t& outServerDate);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
