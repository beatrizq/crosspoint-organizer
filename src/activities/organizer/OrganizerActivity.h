#pragma once
#include <GCalClient.h>
#include <TodoistClient.h>
#include <YnabClient.h>

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Organizer: the tasks, calendar and budget screens behind one top tab bar.
 *
 * Each was a separate home-menu entry before. They are the same kind of thing -
 * a synced, read-mostly list you glance at - so they share a screen, and the tab
 * bar behaves like the settings screen's: selection index 0 focuses the tabs and
 * Select cycles them, 1..n are list rows.
 *
 * Merging them also keeps tab switching free. Each screen deliberately reboots
 * on exit to reclaim Wi-Fi/TLS heap, so leaving one activity for another would
 * have rebooted the device mid-navigation after any sync.
 *
 * Row text is drawn here rather than through GUI.drawList because the list font
 * follows SETTINGS.organizerFontSize; the theme's list draws at a fixed size.
 */
class OrganizerActivity final : public Activity {
 public:
  explicit OrganizerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  enum class Tab : uint8_t { TASKS = 0, CALENDAR = 1, BUDGET = 2 };
  static constexpr int TAB_COUNT = 3;

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
  // Vertical breathing room in a row; scales with the chosen font.
  int rowPadding() const;
  int titleFontId() const;
  int subtitleFontId() const;
  void switchTab(Tab next);
  // The tab Select moves to when the tab bar is focused; wraps at the end.
  Tab nextTab() const { return static_cast<Tab>((static_cast<uint8_t>(tab) + 1) % TAB_COUNT); }
  // Syncs whichever tab is being shown.
  void startSyncForCurrentTab();
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

  // -- budget tab -----------------------------------------------------------
  void startBudgetSync();
  void performBudgetSync();
  static const char* budgetErrorText(YnabClient::Error error);

  ButtonNavigator buttonNavigator;
  Tab tab = Tab::TASKS;
  int selectedIndex = 0;

  State state = State::LIST;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state
  bool wifiActivated = false;
};
