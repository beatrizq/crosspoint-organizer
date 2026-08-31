#pragma once

/**
 * BLE notification relay -- pairing spike, second attempt.
 *
 * Goal: confirm Gadgetbridge (an open-source Android app that already reads
 * every phone notification and can relay them to a wearable over BLE) will
 * discover and pair with this device, before any real notification-count
 * feature is built on top.
 *
 * Gadgetbridge's own device-detection logic (verified against its
 * BangleJSCoordinator/BangleJSConstants source) looks for a peripheral
 * advertising the Nordic UART Service whose name matches "Bangle\.js.*" --
 * so this spike advertises exactly that, does nothing else, and logs
 * whatever it receives. No parsing, no Settings UI, no Home badge yet.
 *
 * First attempt at this (since rolled back) reported itself advertising
 * successfully but was invisible to a raw BLE scanner on the phone. The
 * likely cause, found by comparing against a working reference project: the
 * ESP-IDF Bluetooth controller was never actually enabled at the sdkconfig
 * level (see platformio.ini's CONFIG_BT_ENABLED/CONFIG_BT_NIMBLE_ENABLED),
 * so the NimBLE host's own state machine could report success with no RF
 * ever leaving the radio. This attempt also trims NimBLE's role/connection/
 * bond/CCCD config to the minimum this feature actually needs (see
 * platformio.ini), rather than the library's general-purpose defaults.
 *
 * Entirely compiled out unless ENABLE_BLE_NOTIFY_SPIKE is defined (dev
 * builds only -- see platformio.ini): both methods are no-ops otherwise, so
 * release builds carry no BLE code or NimBLE dependency weight.
 */
class BleNotifyRelay {
 public:
  // Brings up NimBLE in peripheral role and starts advertising. Call once
  // from setup().
  static void begin();

  // Fully tears down NimBLE -- disconnects any peer, stops advertising, and
  // frees every byte NimBLE's host stack holds (mbuf pools, HCI buffers,
  // connection/GATT state). Call before a WiFi connection attempt: a real
  // crash was traced to a bare (non-nothrow) `new` inside Arduino-ESP32's own
  // NetworkEvents::postEvent(), invoked from the WiFi STA event handler,
  // failing under the extra heap pressure BLE's baseline footprint adds --
  // even with no BLE connection active, just from being initialized. Safe to
  // call if BLE is already paused or was never started.
  static void pause();

  // Re-initializes NimBLE and resumes advertising. Call once the WiFi
  // operation that required pause() has finished (success or failure). Safe
  // to call if BLE is already active.
  //
  // NOT currently called anywhere: NimBLEDevice::init()'s task-handle
  // teardown (esp_nimble_disable() in the vendored NimBLE-Arduino ESP32 port,
  // patched by scripts/patch_nimble.py -- see that file's own comment) was
  // confirmed hanging the whole main loop on a deinit()-then-reinit round
  // trip before that patch existed; the patch fixes the one confirmed dead-
  // code bug behind it but has not been round-trip-tested on real hardware.
  // SyncAllActivity -- the only current pause() caller -- deliberately never
  // calls this: it always reboots on exit once WiFi was activated, and
  // begin() re-advertises fresh at boot, so nothing in the current call graph
  // exercises this path. Round-trip test on hardware before wiring resume()
  // into any activity that does NOT already reboot on exit.
  static void resume();

  // Logs a periodic advertising/connected status line, since the one-shot
  // line in begin() is easy to miss on a serial terminal that connects
  // after boot or drops on reboot (this board's USB re-enumerates on every
  // reset). Call from loop(); it self-paces to once per 10s. No-op while
  // paused.
  static void poll();
};
