#pragma once
#include <GCalClient.h>

#include <cstddef>
#include <string>
#include <vector>

#include "OrganizerScreenActivity.h"

/**
 * The Calendar screen: the synced Google Calendar window, tabbed by calendar.
 *
 * Calendar was one tab of the Organizer screen. It is now its own screen, and
 * the tab bar it inherited names the calendars the events came from: All leads,
 * then one tab per calendar ticked in Settings -> Organizer -> Calendar.
 *
 * The tab count therefore follows the selection rather than being fixed, from 1
 * (All alone, nothing ticked) to MAX_CALENDARS + 1. Labels come from the names
 * GCalStore records at pick time, so the bar reads correctly with the radio off;
 * a calendar stored before names were kept shows its id until it is re-ticked.
 *
 * Only All has rows for now. GCalEvent carries no calendar attribution - the
 * fetch merges every calendar into one sorted list - so a per-calendar tab has
 * nothing to filter on until the event carries the calendar it came from.
 */
class CalendarActivity final : public OrganizerScreenActivity {
 public:
  // Tab 0 is All; tab 1..n is selectedCalendars[n - 1].
  static constexpr int ALL_TAB = 0;

  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : OrganizerScreenActivity("Calendar", renderer, mappedInput, ALL_TAB) {}

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return 1 + static_cast<int>(tabCalendars.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // Every event carries its date and time on a second line.
  bool rowsHaveSubtitle() const override { return true; }
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::CALENDAR; }

 private:
  void performCalendarSync();
  void formatEventWhen(int index, char* out, size_t outSize) const;
  static const char* calendarErrorText(GCalClient::Error error);

  // The per-calendar tab labels, snapshotted from GCalStore when the screen
  // opens and after a sync. Held as strings rather than read through the store
  // on demand because tabLabel() hands out a const char* that has to outlive the
  // call, and because the tab count must not change under the selection model
  // mid-screen.
  void rebuildTabs();
  std::vector<std::string> tabCalendars;
};
