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

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onOrganizerOpen();
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
