#pragma once
#include <GCalClient.h>

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Calendar screen: events from the selected Google calendars for today and the
 * next GCAL_WINDOW_DAYS days, rendered from the SD cache so the screen opens
 * with the radio off.
 *
 * It is a list of events, not of days: a date with nothing on it takes no row.
 * Each row shows the event title with its day and time beneath, so a month can
 * be scanned without opening anything.
 *
 * Holding Select brings Wi-Fi up, refreshes the access token and re-fetches the
 * window. Nothing is ever written back to the calendar.
 */
class CalendarActivity final : public Activity {
 public:
  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LIST,     // Showing the cached window
    SYNCING,  // Blocking network work in progress
    FAILED,   // Last sync failed; statusMessage holds the reason
  };

  int eventCount() const;

  void startSync();
  void performSync();
  // "Wed 19 Aug  09:30-10:00", or "Wed 19 Aug  All day". Written into a caller
  // buffer to keep the row builder free of heap churn while scrolling.
  void formatEventWhen(int index, char* out, size_t outSize) const;
  static const char* errorText(GCalClient::Error error);

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::LIST;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state
  bool wifiActivated = false;
};
