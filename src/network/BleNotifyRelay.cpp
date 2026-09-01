#include "BleNotifyRelay.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <NimBLEDevice.h>

#include <cstdlib>
#include <cstring>

#include "BleNotificationQueue.h"

namespace {

// Nordic UART Service: one write-in characteristic (phone -> device), one
// notify-out characteristic (device -> phone). Verified against
// Gadgetbridge's own BangleJSConstants.java (UUID_SERVICE_NORDIC_UART /
// UUID_CHARACTERISTIC_NORDIC_UART_TX / _RX -- named from the phone's own
// perspective there, which is why its "TX" is this device's write-in side).
// Unauthenticated by design: NUS carries no pairing requirement of its own,
// and this code never calls NimBLEDevice::setSecurityAuth(), so no passkey
// dialog or bonding is ever requested from either side.
constexpr char SERVICE_UUID[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char CHAR_WRITE_UUID[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char CHAR_NOTIFY_UUID[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
// Must match Gadgetbridge's BangleJSCoordinator device-name regex
// ("Bangle\.js.*") for its Bangle.js device support to offer pairing at all.
constexpr char DEVICE_NAME[] = "Bangle.js CrossPoint";

// Held for the lifetime of a connection: without it, HalPowerManager throttles
// the CPU to LOW_POWER_FREQ (10 MHz, no-PSRAM board) after 3s of button
// idleness, which starves the NimBLE host task and drops the link unless the
// user keeps touching the device. Single connection by design (see
// CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1 in platformio.ini), so one Lock at a
// time is exactly what HalPowerManager::Lock supports.
std::unique_ptr<HalPowerManager::Lock> connectionLock;

// Tracks whether NimBLE is currently brought up, vs. torn down for pause().
// poll() and resume() use this to no-op/rebuild correctly.
bool active = false;

// bringUp()'s real, dynamic heap cost (NimBLE's heap_caps_malloc-based mbuf
// pools/buffers, connection state, host task stack -- not the static
// .bss/.data a binary-size analysis alone would catch). Folded into poll()'s
// periodic status log below rather than logged once in bringUp() itself,
// since this board's USB re-enumerates on every reset (see this file's own
// header comment) -- a one-shot line at boot is easy to miss before a serial
// terminal reconnects.
int32_t bleInitHeapCost = 0;

// Set for the duration of pause()'s NimBLEDevice::deinit() call. deinit()
// disconnects any connected peer as part of its own teardown, which fires
// onDisconnect() below from *inside* that teardown -- a real crash
// (heap_caps_free assert, "free() target pointer is outside heap areas") came
// from onDisconnect() reacting to that by calling
// NimBLEDevice::startAdvertising(), reentering NimBLE APIs mid-teardown. This
// flag tells onDisconnect() to stay passive when the disconnect is pause()'s
// own doing.
bool pausing = false;

// Reassembly buffer for one GB({...}) command. Gadgetbridge chunks a command
// across multiple ~20-byte BLE writes with no per-chunk framing (BangleJS
// DeviceSupport.java's uartTx()) and terminates it with a single '\n' -- the
// device is expected to buffer until that terminator, not treat each write as
// a complete message.
//
// Sized for the realistic common case, not the pathological worst case:
// Gadgetbridge caps title/subject/sender/body at 80/80/40/400 original
// characters (cropToLength calls in onNotification()), each individually
// hex-escaped as "\xHH" per UTF-8 byte for anything outside plain ASCII (see
// jsonToStringInternal) -- a message that is entirely 2-byte-UTF-8 accented
// text expands to roughly (80+80+40+400)*2 bytes*4 chars/escape =~ 4.8KB
// including JSON overhead. A message saturated with 3-4 byte UTF-8 (dense
// CJK/emoji) could in theory run past that; such a message is simply dropped
// (see the overflow check in onWrite() below) rather than sized for -- a
// permanently-reserved worst-case buffer for a rare edge case is a poor
// trade on this board's RAM budget, and a dropped notification is a graceful
// degradation CLAUDE.md's error-handling philosophy already favors over
// crashing or truncating into invalid JSON.
constexpr size_t CMD_BUFFER_SIZE = 4096;
char cmdBuffer[CMD_BUFFER_SIZE];
size_t cmdLen = 0;
bool cmdOverflowed = false;

// Converts Gadgetbridge's JS-literal string escapes -- "\xHH" (hex byte, used
// for UTF-8 continuation/lead bytes and other bytes standard JSON can't
// express directly), "\v" (vertical tab, not a JSON escape at all), and its
// octal-style forms for other control bytes (e.g. "\20" for byte 16, DLE) --
// into plain bytes ArduinoJson's deserializeJson() can parse. Standard JSON
// escapes ("\" \\ \/ \b \f \n \r \t \uXXXX") are left untouched for
// deserializeJson() itself to handle. Every conversion replaces a longer
// escape sequence with the single byte it represents, so this always shrinks
// or preserves length and can run in place. Returns the new length.
size_t unescapeGadgetbridgeEscapes(char* buf, const size_t len) {
  size_t readIdx = 0;
  size_t writeIdx = 0;
  while (readIdx < len) {
    const char c = buf[readIdx];
    if (c != '\\' || readIdx + 1 >= len) {
      buf[writeIdx++] = c;
      readIdx++;
      continue;
    }
    const char next = buf[readIdx + 1];
    if (next == 'x' && readIdx + 3 < len) {
      const char hex[3] = {buf[readIdx + 2], buf[readIdx + 3], '\0'};
      buf[writeIdx++] = static_cast<char>(strtol(hex, nullptr, 16));
      readIdx += 4;
    } else if (next == 'v') {
      // Not a JSON escape and not meaningful to render -- drop it.
      readIdx += 2;
    } else if (next >= '0' && next <= '7') {
      // Gadgetbridge's octal form, e.g. "\20" for byte 16 (DLE). Consume up
      // to 2 octal digits, matching the longest form it ever emits.
      int value = next - '0';
      size_t digits = 1;
      if (readIdx + 2 < len && buf[readIdx + 2] >= '0' && buf[readIdx + 2] <= '7') {
        value = value * 8 + (buf[readIdx + 2] - '0');
        digits = 2;
      }
      buf[writeIdx++] = static_cast<char>(value);
      readIdx += 1 + digits;
    } else {
      // A standard JSON escape ('"' '\\' '/' 'b' 'f' 'n' 'r' 't') or the start
      // of \uHHHH -- leave both characters for deserializeJson() to handle.
      buf[writeIdx++] = c;
      buf[writeIdx++] = next;
      readIdx += 2;
    }
  }
  return writeIdx;
}

// Handles one fully-reassembled, unescaped command. Anything other than a
// GB({...}) JSON call is some other Espruino snippet Gadgetbridge also sends
// (setTime(...), storage writes, ...) -- nothing here to parse, and nothing
// that needs a reply either way.
void processCommand(char* buf, size_t len) {
  if (len < 4 || buf[0] != 'G' || buf[1] != 'B' || buf[2] != '(' || buf[len - 1] != ')') {
    LOG_DBG("BLE", "Command (not GB JSON): %.*s", static_cast<int>(len), buf);
    return;
  }

  char* json = buf + 3;
  const size_t rawJsonLen = len - 4;  // strip leading "GB(" and trailing ")"
  const size_t jsonLen = unescapeGadgetbridgeEscapes(json, rawJsonLen);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, jsonLen);
  if (err) {
    LOG_DBG("BLE", "GB() JSON parse failed: %s", err.c_str());
    return;
  }

  const char* type = doc["t"] | "";
  uint8_t hour = 0;
  uint8_t minute = 0;

  if (strcmp(type, "notify") == 0) {
    const char* src = doc["src"] | "";
    const char* title = doc["title"] | "";
    const char* body = doc["body"] | "";
    const uint32_t id = doc["id"] | static_cast<uint32_t>(0);
    halClock.getTime(hour, minute);
    BLE_NOTIFICATIONS.push(id, false, src, title, body, hour, minute);
    BLE_NOTIFICATIONS.saveToFile();
    LOG_INF("BLE", "Notification from %s: %s: %s", src, title, body);
  } else if (strcmp(type, "call") == 0) {
    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "incoming") == 0) {
      const char* name = doc["name"] | "";
      const char* number = doc["number"] | "";
      const bool hasName = name[0] != '\0';
      halClock.getTime(hour, minute);
      // The number rides in `content` only when a name is already shown as the
      // title -- otherwise the number IS the title, and repeating it in
      // content would just duplicate the line.
      BLE_NOTIFICATIONS.push(0, true, tr(STR_BLE_INCOMING_CALL), hasName ? name : number, hasName ? number : "", hour,
                             minute);
      BLE_NOTIFICATIONS.saveToFile();
      LOG_INF("BLE", "Incoming call: %s / %s", name, number);
    }
    // Other cmd values (accept/reject/outgoing/start/end, ...) aren't shown --
    // this queue is "who tried to reach me", not a full call-state mirror.
  }
  // "notify-" (remote dismiss) and every other "t" (weather, musicinfo,
  // actfetch, is_gps_active, ...): see BleNotificationQueue's own doc comment
  // for why dismiss isn't tracked. No reply required for any of these either
  // way -- confirmed against Gadgetbridge's own BangleJSDeviceSupport.java,
  // which just logs "packet type '...' not understood" and moves on.
}

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& connInfo) override {
    LOG_INF("BLE", "Connected: %s", connInfo.getAddress().toString().c_str());
    connectionLock = makeUniqueNoThrow<HalPowerManager::Lock>();
    if (!connectionLock) {
      LOG_ERR("BLE", "OOM: HalPowerManager::Lock (%u bytes)", static_cast<unsigned>(sizeof(HalPowerManager::Lock)));
    }
  }

  void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/, const int reason) override {
    connectionLock.reset();
    if (pausing) {
      LOG_INF("BLE", "Disconnected (reason %d) -- pausing, not resuming advertising", reason);
      return;
    }
    LOG_INF("BLE", "Disconnected (reason %d) -- resuming advertising", reason);
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(const uint16_t mtu, NimBLEConnInfo& /*connInfo*/) override { LOG_INF("BLE", "MTU: %u", mtu); }
};

class WriteCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& /*connInfo*/) override {
    const auto& value = chr->getValue();
    const char* data = value.c_str();
    const size_t len = value.length();

    for (size_t i = 0; i < len; i++) {
      const char b = data[i];
      // DLE (0x10): Espruino console convention for "discard whatever is
      // currently buffered" -- Gadgetbridge itself prefixes every command
      // with this byte (uartTxJSON's "GB(...)" framing), so treating it
      // as a hard reset here also makes us robust to a prior command that
      // never reached its '\n' (e.g. a dropped BLE packet).
      if (b == '\x10') {
        cmdLen = 0;
        cmdOverflowed = false;
        continue;
      }
      if (b == '\n') {
        if (cmdOverflowed) {
          LOG_ERR("BLE", "Command exceeded %u-byte buffer, discarded", static_cast<unsigned>(CMD_BUFFER_SIZE));
        } else if (cmdLen > 0) {
          processCommand(cmdBuffer, cmdLen);
        }
        cmdLen = 0;
        cmdOverflowed = false;
        continue;
      }
      if (cmdLen >= CMD_BUFFER_SIZE) {
        cmdOverflowed = true;  // Logged once, on the '\n' above, not per byte.
        continue;
      }
      cmdBuffer[cmdLen++] = b;
    }
  }

  void onSubscribe(NimBLECharacteristic* /*chr*/, NimBLEConnInfo& /*connInfo*/, const uint16_t subValue) override {
    LOG_INF("BLE", "Subscribe state: %u", subValue);
  }
};

ServerCallbacks serverCallbacks;
WriteCallbacks writeCallbacks;

// Builds the server/service/characteristics from scratch and starts
// advertising. Called once, from begin() only -- resume() reuses these same
// objects rather than rebuilding, since pause()'s deinit(false) leaves them
// intact by design (see the deinit(bool) doc comment in NimBLEDevice.h).
void bringUp() {
  const uint32_t freeHeapBefore = ESP.getFreeHeap();

  NimBLEDevice::init(DEVICE_NAME);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  NimBLEService* service = server->createService(SERVICE_UUID);
  NimBLECharacteristic* writeChar =
      service->createCharacteristic(CHAR_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  writeChar->setCallbacks(&writeCallbacks);
  service->createCharacteristic(CHAR_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);
  // No NimBLEService::start() call: it is a deprecated no-op in this library
  // version (2.5.x) -- the server and its services start together, below.

  // A legacy advertising packet is capped at 31 bytes; the name (23 bytes)
  // and the 128-bit service UUID (18 bytes) don't both fit alongside the
  // 3-byte flags structure NimBLE always adds. Whichever doesn't fit spills
  // into the scan response automatically -- but enableScanResponse() must be
  // called first for that overflow to be decided the way we want: the
  // service UUID is what a scanner's device-detection filter actually keys
  // on, so it needs to land in the primary packet; the name can safely ride
  // in scan response, since anything doing a real pairing flow (not just a
  // passive scan) reads both packets anyway.
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->addServiceUUID(service->getUUID());
  advertising->setName(DEVICE_NAME);
  if (!advertising->start()) {
    LOG_ERR("BLE", "Failed to start advertising -- Gadgetbridge will never see this device");
    return;
  }

  active = true;
  bleInitHeapCost = static_cast<int32_t>(freeHeapBefore) - static_cast<int32_t>(ESP.getFreeHeap());
  LOG_INF("BLE", "Advertising as \"%s\" for Gadgetbridge pairing", DEVICE_NAME);
}

}  // namespace

void BleNotifyRelay::begin() { bringUp(); }

void BleNotifyRelay::pause() {
  if (!active) return;
  // deinit()'s clearAll only controls whether it deletes our server/
  // advertising/service objects -- the actual heap-freeing work
  // (nimble_port_stop()/nimble_port_deinit(): host task, mbuf pools, HCI
  // buffers, connection state -- the real fix for the original OOM crash)
  // happens unconditionally either way. clearAll=true (deleting and later
  // rebuilding those objects) hit three separate internal NimBLE-Arduino
  // bugs across this project's own testing: a reentrant onDisconnect callback
  // during teardown, a central-role m_pClient deletion on an uninitialized
  // pointer, and a corrupted std::vector free in NimBLEServer's destructor.
  // clearAll=false frees the same heap without ever running any of those
  // destructors, and is also this library's own documented mechanism for
  // exactly this "pause temporarily, resume later" use case.
  pausing = true;
  connectionLock.reset();
  NimBLEDevice::deinit(false);
  pausing = false;
  active = false;
  LOG_INF("BLE", "Paused for WiFi -- disconnected any peer, freed NimBLE's heap");
}

void BleNotifyRelay::resume() {
  if (active) return;
  // Re-init only: the server/advertising/service objects created once in
  // bringUp() (called only from begin()) survived pause()'s deinit(false)
  // untouched -- NimBLEDevice::createServer()/getAdvertising() are both
  // lazy singletons that return the existing object rather than creating a
  // new one, so calling bringUp() again here would be redundant at best and
  // risk a duplicate service at worst.
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising->start()) {
    LOG_ERR("BLE", "Failed to resume advertising -- Gadgetbridge will never see this device");
    return;
  }
  active = true;
  LOG_INF("BLE", "Resumed after WiFi operation finished");
}

void BleNotifyRelay::poll() {
  static unsigned long lastStatusLog = 0;
  const unsigned long now = millis();
  if (now - lastStatusLog < 10000) return;
  lastStatusLog = now;

  // Always logs something, even if bringUp() never got as far as setting
  // active=true -- silence here was previously ambiguous between "BLE not
  // compiled in", "bringUp() failed silently", and "succeeded but something
  // else suppressed logging", and diagnosing that live is hard on this board
  // (USB CDC re-enumerates on every reset, dropping any serial session).
  if (!active) {
    LOG_INF("BLE", "Status: NOT ACTIVE (bringUp() never completed successfully)");
    return;
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  NimBLEServer* server = NimBLEDevice::getServer();
  const bool isAdvertising = advertising != nullptr && advertising->isAdvertising();
  const unsigned connectedCount = server != nullptr ? static_cast<unsigned>(server->getConnectedCount()) : 0;
  LOG_INF("BLE", "Status: advertising=%d connected=%u, init heap cost=%d bytes", isAdvertising, connectedCount,
          bleInitHeapCost);
}

#else

void BleNotifyRelay::begin() {}
void BleNotifyRelay::pause() {}
void BleNotifyRelay::resume() {}
void BleNotifyRelay::poll() {}

#endif
