#pragma once
#include <GCalClient.h>

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Picks which of the linked account's calendars feed the Calendar screen.
 *
 * The list is fetched live rather than cached: it changes rarely, it is only
 * ever opened deliberately, and a stale list would silently drop a calendar the
 * user had just created. Selection is stored as calendar ids in GCalStore.
 */
class GCalCalendarPickerActivity final : public Activity {
 public:
  explicit GCalCalendarPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GCalCalendarPicker", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LOADING,  // Fetching the calendar list
    LIST,
    FAILED,
  };

  void fetchCalendars();
  void toggleSelected();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::LOADING;
  const char* statusMessage = nullptr;
  std::vector<GCalClient::CalendarInfo> calendars;
  bool wifiActivated = false;
  bool dirty = false;  // Selection changed and needs persisting on exit
};
