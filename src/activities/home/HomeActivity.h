#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  // Home can be entered while Back is still held (e.g. leaving Settings with
  // Back): ignore that stale release until a fresh press is seen here.
  bool backPressSeen = false;
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  // One row of the home screen, in the order it is drawn.
  //
  // This replaces the index arithmetic that used to be spelled out in three
  // places - a count, a menu-item-to-index map and its inverse - which had to
  // be kept in step by hand, and which fixed the recent books at the front.
  // Order is now whatever buildEntries() appends.
  struct HomeEntry {
    const char* label;  // Translated; unused for cover-tile entries
    UIIcon icon;
    HomeMenuItem item;  // NONE when the entry opens a book
    int recentIndex;    // >= 0 when the entry opens recentBooks[recentIndex]
  };

  std::vector<HomeEntry> entries;

  // Rebuilt in onEnter, once the recent books and the OPDS check are in: the
  // list is walked every loop() and must not allocate there.
  void buildEntries();
  // Rows the cover tile owns, which the menu below it does not draw. Zero on
  // themes that carry reading as a menu entry instead.
  int leadingRecentCount() const;

  // Where the companion sits in the cycle order, or -1 when it is off or the
  // theme gave it no room. Not an entries[] index -- the companion is not a
  // grid tile or a book, it is an extra stop inserted right after the
  // leading cover(s) and before the first grid tile, so the same Prev/Next
  // buttons and swipes that already cycle the menu pass through it too. Any
  // selectorIndex at or past this value that isn't exactly this value maps to
  // entries[selectorIndex - 1] instead of entries[selectorIndex] -- see
  // toEntryIndex/fromEntryIndex in the .cpp.
  int companionSlotIndex() const;

  // Hold threshold for "sync everything" on the Settings button. The same
  // 1000ms the organizer screens use for their hold-to-sync, so one gesture
  // means one thing across the firmware.
  static constexpr unsigned long SYNC_ALL_HOLD_MS = 1000;

  // Rotates the companion's line so each visit to Home gets a different one
  // while it stays stable as the cursor moves around the menu.
  uint32_t companionQuoteIndex = 0;
  // Advances on every home repaint to drive the walk cycle. Driven by redraws
  // rather than a timer, so the character only moves when the screen was going
  // to be painted anyway - a timer would keep the panel refreshing, block the
  // low-power idle and accumulate e-ink ghosting.
  uint32_t companionFrame = 0;

  // Draws the companion, its speech bubble and its status into the column the
  // theme set aside inside the cover card. No-op when disabled, or when the
  // theme handed back no room. Draws a focus outline when focused is true.
  void drawCompanion(Rect region, bool focused) const;
  // Rolls a weighted-random pending task/habit and opens QuickPickActivity
  // with it. No-op when the companion is off or has no room on this theme --
  // there is nothing to have focused in that case.
  void activateCompanion();

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
