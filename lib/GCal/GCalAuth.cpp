#include "GCalAuth.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <CivilTime.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include "GCalStore.h"

int GCalAuth::lastHttpCode = 0;

namespace {

constexpr char DEVICE_CODE_URL[] = "https://oauth2.googleapis.com/device/code";
constexpr char TOKEN_URL[] = "https://oauth2.googleapis.com/token";

// Percent-encoded "https://www.googleapis.com/auth/calendar.readonly".
constexpr char SCOPE_ENCODED[] = "https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcalendar.readonly";

constexpr char GRANT_DEVICE[] = "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";

// OAuth replies are small flat objects (a few hundred bytes), so they are
// buffered and handed to ArduinoJson rather than streamed. The events list is
// the one that needs streaming.
constexpr size_t TOKEN_DOC_CAPACITY = 2048;

// Same TLS heap gate as TodoistClient: the wolfSSL handshake needs working heap,
// and a doomed attempt costs ~15s before it gives up.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("GCA", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap, MIN_FREE_FOR_TLS,
            maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

// Maps the OAuth `error` field to our enum. RFC 8628 defines the first four;
// invalid_grant means the refresh token was revoked or the app was removed.
GCalAuth::Error errorForOAuthCode(const char* code) {
  if (strcmp(code, "authorization_pending") == 0) return GCalAuth::AUTH_PENDING;
  if (strcmp(code, "slow_down") == 0) return GCalAuth::SLOW_DOWN;
  if (strcmp(code, "access_denied") == 0) return GCalAuth::ACCESS_DENIED;
  if (strcmp(code, "expired_token") == 0) return GCalAuth::CODE_EXPIRED;
  if (strcmp(code, "invalid_grant") == 0) return GCalAuth::INVALID_GRANT;
  return GCalAuth::NETWORK_ERROR;
}

// Issues a form-encoded POST and parses the JSON reply. outDoc is filled on any
// reply that parsed, including error replies, so the caller can read `error`.
GCalAuth::Error postForm(const char* url, const std::string& body, JsonDocument& outDoc, uint16_t* outServerDate) {
  if (insufficientHeap()) return GCalAuth::LOW_MEMORY;

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("GCA", "Bad URL: %s", url);
    return GCalAuth::NETWORK_ERROR;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Accept", "application/json");

  const int code = http.POST(body);
  GCalAuth::lastHttpCode = code;

  if (outServerDate != nullptr) {
    // Read before end(): the parsed headers belong to this connection.
    const std::string date = http.getHeader("date");
    *outServerDate = date.empty() ? civil::NO_DATE : civil::dateFromHttpHeader(date.c_str());
  }

  const std::string payload = http.getString();
  http.end();

  if (code <= 0) {
    LOG_ERR("GCA", "Transport failure (%d) for %s", code, url);
    return GCalAuth::NETWORK_ERROR;
  }
  if (payload.size() > TOKEN_DOC_CAPACITY) {
    LOG_ERR("GCA", "OAuth reply too large: %zu bytes", payload.size());
    return GCalAuth::PARSE_ERROR;
  }
  if (deserializeJson(outDoc, payload) != DeserializationError::Ok) {
    LOG_ERR("GCA", "Malformed OAuth JSON (HTTP %d)", code);
    return GCalAuth::PARSE_ERROR;
  }

  const char* err = outDoc["error"] | "";
  if (err[0] != '\0') {
    const GCalAuth::Error mapped = errorForOAuthCode(err);
    // Pending and slow_down are the normal rhythm of the flow, not faults.
    if (mapped == GCalAuth::AUTH_PENDING || mapped == GCalAuth::SLOW_DOWN) {
      LOG_DBG("GCA", "OAuth: %s", err);
    } else {
      LOG_ERR("GCA", "OAuth error: %s (HTTP %d)", err, code);
    }
    return mapped;
  }
  if (code < 200 || code >= 300) {
    LOG_ERR("GCA", "Unexpected HTTP %d from %s", code, url);
    return GCalAuth::NETWORK_ERROR;
  }
  return GCalAuth::OK;
}

}  // namespace

GCalAuth::Error GCalAuth::requestDeviceCode(DeviceCode& out) {
  if (!GCAL_STORE.hasClientCredentials()) return NO_CLIENT;

  std::string body = "client_id=";
  body += GCAL_STORE.getClientId();
  body += "&scope=";
  body += SCOPE_ENCODED;

  JsonDocument doc;
  const Error status = postForm(DEVICE_CODE_URL, body, doc, nullptr);
  if (status != OK) return status;

  out.deviceCode = doc["device_code"] | "";
  out.userCode = doc["user_code"] | "";
  // Google returns verification_url; RFC 8628 names it verification_uri.
  out.verificationUrl = doc["verification_url"] | "";
  if (out.verificationUrl.empty()) out.verificationUrl = doc["verification_uri"] | "";
  out.intervalSec = doc["interval"] | 5U;
  out.expiresInSec = doc["expires_in"] | 1800U;

  if (out.deviceCode.empty() || out.userCode.empty()) {
    LOG_ERR("GCA", "Device code reply missing required fields");
    return PARSE_ERROR;
  }
  LOG_INF("GCA", "Device code issued, user code %s, interval %us", out.userCode.c_str(), out.intervalSec);
  return OK;
}

GCalAuth::Error GCalAuth::pollForTokens(const std::string& deviceCode, std::string& outRefreshToken,
                                        std::string& outAccessToken) {
  if (!GCAL_STORE.hasClientCredentials()) return NO_CLIENT;

  std::string body = "client_id=";
  body += GCAL_STORE.getClientId();
  body += "&client_secret=";
  body += GCAL_STORE.getClientSecret();
  body += "&device_code=";
  body += deviceCode;
  body += "&grant_type=";
  body += GRANT_DEVICE;

  JsonDocument doc;
  const Error status = postForm(TOKEN_URL, body, doc, nullptr);
  if (status != OK) return status;

  outAccessToken = doc["access_token"] | "";
  outRefreshToken = doc["refresh_token"] | "";
  if (outRefreshToken.empty()) {
    // Without this the link would not survive a reboot, which defeats the flow.
    LOG_ERR("GCA", "Token reply carried no refresh_token");
    return PARSE_ERROR;
  }
  LOG_INF("GCA", "Account linked");
  return OK;
}

GCalAuth::Error GCalAuth::refreshAccessToken(std::string& outAccessToken, uint16_t& outServerDate) {
  outAccessToken.clear();
  outServerDate = civil::NO_DATE;
  if (!GCAL_STORE.hasClientCredentials()) return NO_CLIENT;
  if (!GCAL_STORE.isLinked()) return INVALID_GRANT;

  std::string body = "client_id=";
  body += GCAL_STORE.getClientId();
  body += "&client_secret=";
  body += GCAL_STORE.getClientSecret();
  body += "&refresh_token=";
  body += GCAL_STORE.getRefreshToken();
  body += "&grant_type=refresh_token";

  JsonDocument doc;
  const Error status = postForm(TOKEN_URL, body, doc, &outServerDate);
  if (status != OK) return status;

  outAccessToken = doc["access_token"] | "";
  if (outAccessToken.empty()) {
    LOG_ERR("GCA", "Refresh reply carried no access_token");
    return PARSE_ERROR;
  }
  char iso[11];
  civil::isoFromDate(outServerDate, iso, sizeof(iso));
  LOG_DBG("GCA", "Access token refreshed; server date %s", iso[0] != '\0' ? iso : "unknown");
  return OK;
}

const char* GCalAuth::errorString(const Error error) {
  switch (error) {
    case OK:
      return "ok";
    case NO_CLIENT:
      return "client credentials not set";
    case NETWORK_ERROR:
      return "network error";
    case AUTH_PENDING:
      return "waiting for approval";
    case SLOW_DOWN:
      return "polling too fast";
    case ACCESS_DENIED:
      return "access denied";
    case CODE_EXPIRED:
      return "code expired";
    case INVALID_GRANT:
      return "link revoked";
    case PARSE_ERROR:
      return "bad response";
    case LOW_MEMORY:
      return "low memory";
  }
  return "unknown";
}
