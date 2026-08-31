#include "BleNotifyRelay.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <Arduino.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Memory.h>
#include <NimBLEDevice.h>

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
    LOG_INF("BLE", "Write (%u bytes): %.*s", static_cast<unsigned>(value.length()), static_cast<int>(value.length()),
            value.c_str());
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
