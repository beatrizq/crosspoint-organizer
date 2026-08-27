#include "ReadMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RecentBookLoader.h"

void ReadMenuActivity::onEnter() {
  Activity::onEnter();
  buildEntries();
  recentBooks = recentBookLoader::load(1);
  selectedIndex = 0;
  requestUpdate();
}

void ReadMenuActivity::buildEntries() {
  entries.clear();
  entries.reserve(4);
  entries.push_back({tr(STR_BROWSE_FILES), Folder, HomeMenuItem::FILE_BROWSER});
  entries.push_back({tr(STR_MENU_RECENT_BOOKS), Recent, HomeMenuItem::RECENTS});
  if (OPDS_STORE.hasServers()) {
    entries.push_back({tr(STR_OPDS_BROWSER), Library, HomeMenuItem::OPDS_BROWSER});
  }
  entries.push_back({tr(STR_FILE_TRANSFER), Transfer, HomeMenuItem::FILE_TRANSFER});
}

void ReadMenuActivity::activateSelected() {
  if (selectedIndex == 0) {
    // The leading card slot always exists (see render()); nothing to open
    // when there is no recent book, same as before this screen carried the
    // card at all.
    if (!recentBooks.empty()) activityManager.goToReader(recentBooks[0].path);
    return;
  }
  const int entryIdx = selectedIndex - 1;
  if (entryIdx < 0 || entryIdx >= static_cast<int>(entries.size())) return;
  switch (entries[entryIdx].item) {
    case HomeMenuItem::FILE_BROWSER:
      activityManager.goToFileBrowser();
      break;
    case HomeMenuItem::RECENTS:
      activityManager.goToRecentBooks();
      break;
    case HomeMenuItem::OPDS_BROWSER:
      activityManager.goToBrowser();
      break;
    case HomeMenuItem::FILE_TRANSFER:
      activityManager.goToFileTransfer();
      break;
    default:
      break;
  }
}

void ReadMenuActivity::loop() {
  const int itemCount = static_cast<int>(entries.size()) + 1;  // +1 for the leading card slot

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::READ_MENU);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Checked ahead of the regular row touch zone below, which starts only
  // after this one -- same idea as HomeActivity's own companion-vs-cover
  // touch ordering. Always present, same rect render() draws the card in,
  // matching drawRecentBookCover()'s own "no book yet" placeholder when
  // recentBooks is empty rather than hiding the slot.
  {
    const Rect cardRect{0, listTop, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
    int cx = 0;
    int cy = 0;
    if (mappedInput.wasScreenTouchDown(cx, cy) && cx >= cardRect.x && cx < cardRect.x + cardRect.width &&
        cy >= cardRect.y && cy < cardRect.y + cardRect.height) {
      if (selectedIndex != 0) {
        selectedIndex = 0;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasTapInRect(cardRect.x, cardRect.y, cardRect.width, cardRect.height)) {
      selectedIndex = 0;
      activateSelected();
      return;
    }
    listTop += metrics.homeCoverTileHeight + metrics.menuSpacing;
  }

  int row = -1;
  const auto touch = mappedInput.rowTouch(row, listTop, metrics.menuRowHeight + metrics.menuSpacing,
                                          static_cast<int>(entries.size()), 0, INT32_MAX, metrics.menuRowHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    const int touchedIndex = row + 1;
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectedIndex != touchedIndex) {
        selectedIndex = touchedIndex;
        requestUpdate();
      }
      return;
    }
    selectedIndex = touchedIndex;
    activateSelected();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void ReadMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_READ), nullptr);

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // The same per-theme card Home used to draw at the top of its own screen
  // (see LyraTheme/BaseTheme/RoundedRaffTheme's drawRecentBookCover()) --
  // reused as-is rather than a bespoke row, so this screen shows exactly
  // whatever "cover together with title and author, selection cue included"
  // means for the active theme, with nothing invented on top of it. No
  // buffer-caching here unlike Home: nothing else repaints over this card
  // every frame the way the companion's walk animation does, so there is
  // nothing worth caching a redraw for.
  bool coverRendered = false;
  bool coverBufferStored = false;
  bool bufferRestored = false;
  GUI.drawRecentBookCover(renderer, Rect{0, contentTop, pageWidth, metrics.homeCoverTileHeight}, recentBooks,
                          selectedIndex, coverRendered, coverBufferStored, bufferRestored, [] { return false; });
  contentTop += metrics.homeCoverTileHeight + metrics.menuSpacing;

  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const auto& rows = entries;
  GUI.drawButtonMenu(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex - 1,
      [&rows](int index) { return std::string(rows[index].label); }, [&rows](int index) { return rows[index].icon; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
