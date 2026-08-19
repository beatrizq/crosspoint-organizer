#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

/**
 * Shared chrome for the three organizer screens: Tasks, Calendar and Budget.
 *
 * These were one screen with a Tasks/Calendar/Budget tab bar. They are now a
 * screen each, reached from its own home tile, and the tab bar belongs to the
 * screen rather than to the set: Tasks tabs between Overdue, Today and
 * Upcoming, Calendar shows Schedule, Budget tabs between Plan and Account. A
 * tab now switches the view within one subject instead of switching subject.
 *
 * What every one of them still does identically lives here - the header and tab
 * bar, the selection model, the Wi-Fi/sync/teardown dance, the paged list and
 * the button hints - so splitting the screens apart did not triplicate it.
 * Subclasses supply their tabs, their rows, and their sync.
 *
 * Selection index 0 focuses the tab bar and 1..n are list rows, as on the
 * settings screen. Rows are drawn by the subclass rather than through
 * GUI.drawList because the list font follows SETTINGS.organizerFontSize; the
 * theme's list draws at a fixed size.
 *
 * Each screen deliberately reboots on exit to reclaim Wi-Fi/TLS heap, so
 * switching tabs stays free but leaving the screen after a sync costs a silent
 * restart. That was already true of the merged screen; what changed is that
 * Tasks -> Calendar is now a screen change and so pays it, where before it was
 * a tab switch.
 */
class OrganizerScreenActivity : public Activity {
 public:
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  enum class State : uint8_t {
    LIST,     // Showing the cached list
    SYNCING,  // Blocking network work in progress
    FAILED,   // Last sync failed; statusMessage holds the reason
  };

  // Everything a subclass needs to draw one row, so it neither recomputes the
  // metrics the base already resolved nor has to know about paging.
  struct RowLayout {
    int index;         // Row in the subclass's own list
    int x;             // Left edge of the text column
    int y;             // Top of the row band
    int width;         // Width of the text column
    int height;        // Full row height, separator included
    int textY;         // Top of the title line
    int titleFont;     // Follows SETTINGS.organizerFontSize
    int subtitleFont;  // One step below the title
    bool ink;          // False on the selected row, whose fill is black
  };

  OrganizerScreenActivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput, int initialTab);

  // -- what each screen supplies --------------------------------------------

  // Drawn on the left of the header.
  virtual const char* screenTitle() const = 0;
  virtual int tabCount() const = 0;
  virtual const char* tabLabel(int index) const = 0;
  // The header's right-hand summary for the tab being shown. Write "" to draw
  // nothing.
  virtual void formatStatus(char* out, size_t outSize) const = 0;
  // Rows in the tab being shown.
  virtual int rowCount() const = 0;
  virtual void drawRow(const RowLayout& layout) const = 0;
  // Centred when rowCount() is 0, and while a sync is blocking.
  virtual const char* emptyMessage() const = 0;
  virtual const char* syncingMessage() const = 0;
  // Checks its preconditions, then calls runSync() with the network work.
  virtual void startSync() = 0;

  // -- optional -------------------------------------------------------------

  // Second line per row; taken into account by listRowHeight().
  virtual bool rowsHaveSubtitle() const { return false; }
  // Select on a row: the hint, and what it does. The default leaves the button
  // unlabelled, which is right for a read-only list.
  virtual const char* rowConfirmLabel() const { return ""; }
  virtual void onRowConfirm() {}
  // Loads whatever caches the screen renders from. Called before the first
  // paint, so the list is on screen without needing Wi-Fi.
  virtual void loadCaches() {}
  // Called after the tab changed, for a screen that has per-tab state to reset.
  virtual void onTabChanged() {}
  // The tile home should reselect when this screen exits.
  virtual HomeMenuItem homeItem() const { return HomeMenuItem::NONE; }

  // -- state a subclass reads and writes ------------------------------------

  int tab() const { return activeTab; }
  // selectedIndex 0 is the tab bar, so a row is selectedIndex - 1.
  int selectedRow() const { return selectedIndex - 1; }
  State getState() const { return state; }

  // Shows SYNCING, brings the radio up - asking for a network when there is no
  // connection yet - and then runs work(). Every screen's sync funnels through
  // here so the radio is owned in one place.
  void runSync(std::function<void()> work);
  // Puts the screen in FAILED with a translated reason and repaints.
  void failSync(const char* message);
  // Called by a subclass once its network work is done: settles the list state
  // and repaints. Pass nullptr as the message on success.
  void finishSync(const char* failureMessage);
  // Takes the radio down after a sync, through the Arduino layer so its own
  // state goes down with it, and records that it happened.
  void tearDownRadio();

  int titleFontId() const;
  int subtitleFontId() const;
  // Vertical breathing room in a row; scales with the chosen font.
  int rowPadding() const;
  int listRowHeight() const;

  State state = State::LIST;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state
  int selectedIndex = 0;

  // The confirmation popup acts on the button going down, so the release lands
  // back here - in a screen where Select may complete a task. Set while that
  // release is still owed, so it is dropped rather than acted on.
  bool swallowConfirmRelease = false;

 private:
  // The tab Select moves to when the tab bar is focused; wraps at the end.
  int nextTab() const { return tabCount() <= 1 ? activeTab : (activeTab + 1) % tabCount(); }
  void switchTab(int next);
  // Geometry the input and render paths must agree on.
  int listTop() const;
  int listHeight() const;
  int pageItems() const;

  ButtonNavigator buttonNavigator;
  int activeTab;  // Set from the constructor's initialTab
  bool wifiActivated = false;
  bool radioTornDown = false;
};
