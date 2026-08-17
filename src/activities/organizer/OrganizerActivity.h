#pragma once
#include <GCalClient.h>
#include <TodoistClient.h>

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Organizer: the tasks and calendar screens behind one top tab bar.
 *
 * Both were separate home-menu entries before. They are the same kind of thing -
 * a synced, read-mostly list you glance at - so they share a screen, and the tab
 * bar behaves like the settings screen's: selection index 0 focuses the tabs and
 * Select cycles them, 1..n are list rows.
 *
 * Merging them also keeps tab switching free. Each screen deliberately reboots
 * on exit to reclaim Wi-Fi/TLS heap, so leaving one activity for the other would
 * have rebooted the device mid-navigation after any sync.
 *
 * Row text is drawn here rather than through GUI.drawList because the list font
 * follows SETTINGS.organizerFontSize; the theme's list draws at a fixed size.
 */
class OrganizerActivity final : public Activity {
 public:
  explicit OrganizerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  enum class Tab : uint8_t { TASKS = 0, CALENDAR = 1 };
  static constexpr int TAB_COUNT = 2;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LIST,     // Showing the cached list
    SYNCING,  // Blocking network work in progress
    FAILED,   // Last sync failed; statusMessage holds the reason
  };

  // -- shared ---------------------------------------------------------------
  int rowCount() const;
  int listRowHeight() const;
  int titleFontId() const;
  int subtitleFontId() const;
  void switchTab(Tab next);
  // selectedIndex 0 is the tab bar, so a row is selectedIndex - 1.
  int selectedRow() const { return selectedIndex - 1; }

  // -- tasks tab ------------------------------------------------------------
  void completeSelectedTask();
  void startTaskSync();
  void performTaskSync();
  bool resolveTodayDate(std::string& outDate) const;
  void saveSleepWallpaper() const;
  static const char* taskErrorText(TodoistClient::Error error);

  // -- calendar tab ---------------------------------------------------------
  void startCalendarSync();
  void performCalendarSync();
  void formatEventWhen(int index, char* out, size_t outSize) const;
  static const char* calendarErrorText(GCalClient::Error error);

  ButtonNavigator buttonNavigator;
  Tab tab = Tab::TASKS;
  int selectedIndex = 0;

  State state = State::LIST;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state
  bool wifiActivated = false;
};
