#include "ReadMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ReadMenuActivity::onEnter() {
  Activity::onEnter();
  buildEntries();
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
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) return;
  switch (entries[selectedIndex].item) {
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
  const int itemCount = static_cast<int>(entries.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::READ_MENU);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  int row = -1;
  const auto touch = mappedInput.rowTouch(row, contentTop, metrics.menuRowHeight + metrics.menuSpacing, itemCount, 0,
                                          INT32_MAX, metrics.menuRowHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectedIndex != row) {
        selectedIndex = row;
        requestUpdate();
      }
      return;
    }
    selectedIndex = row;
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

  (void)contentHeight;
}

void ReadMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_READ), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const auto& rows = entries;
  GUI.drawButtonMenu(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
      [&rows](int index) { return std::string(rows[index].label); }, [&rows](int index) { return rows[index].icon; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
