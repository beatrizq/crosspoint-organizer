#pragma once
#include <GCalAuth.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the Google Calendar integration.
 *
 * Holds the one-time setup: the OAuth client id and secret from the owner's own
 * Google Cloud project, linking the account, and choosing which calendars feed
 * the Calendar screen. Syncing itself happens on that screen, not here.
 *
 * Linking runs the device authorization grant, so this screen also acts as the
 * pairing display: it shows the short user code and the URL to enter it at,
 * then polls until Google reports the approval. Polling happens from loop() on
 * the interval Google asks for, so Back still cancels while it waits.
 */
class GCalSettingsActivity final : public Activity {
 public:
  explicit GCalSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GCalSettings", renderer, mappedInput) {}

  // Client ID, Client Secret, Link/Unlink, Calendars, and the sync hint row.
  static constexpr int MENU_ITEMS = 5;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    MENU,     // The settings list
    PAIRING,  // Showing the user code and polling for approval
    FAILED,   // statusMessage holds the reason
  };

  void handleSelection();
  void beginLinking();
  void requestPairingCode();
  void pollPairing();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::MENU;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state

  GCalAuth::DeviceCode pairing;
  unsigned long nextPollMs = 0;     // millis() of the next allowed poll
  unsigned long pairingDeadlineMs = 0;
  bool wifiActivated = false;
};
