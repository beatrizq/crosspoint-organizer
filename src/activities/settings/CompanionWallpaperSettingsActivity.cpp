#include "CompanionWallpaperSettingsActivity.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionSprites.generated.h"
#include "companion/CompanionWallpaperStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Basename only -- the full SD path is what's stored, but a row has no room
// for it and the folder rarely tells the user anything the filename doesn't.
std::string baseName(const std::string& path) {
  const auto pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}
}  // namespace

void CompanionWallpaperSettingsActivity::onEnter() {
  Activity::onEnter();
  COMPANION_WALLPAPERS.loadFromFile();
  selectedIndex = 0;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void CompanionWallpaperSettingsActivity::onExit() { Activity::onExit(); }

void CompanionWallpaperSettingsActivity::openPicker(const int moodIndex) {
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickImage),
      [this, moodIndex](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = true;
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const auto* path = std::get_if<FilePathResult>(&result.data);
        if (!path) return;
        COMPANION_WALLPAPERS.pathForMood[static_cast<size_t>(moodIndex)] = path->path;
        COMPANION_WALLPAPERS.saveToFile();
        requestUpdate();
      });
}

void CompanionWallpaperSettingsActivity::handleSelection() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(companion::MOOD_COUNT)) return;
  const int moodIndex = selectedIndex;

  std::vector<std::string> options{tr(STR_CHOOSE_FILE), tr(STR_CLEAR_BUTTON)};
  optionPopup.show(StrId::STR_COMPANION_MOOD_WALLPAPERS, options, -1, [this, moodIndex](const int choice) {
    if (choice == 0) {
      openPicker(moodIndex);
    } else if (choice == 1) {
      COMPANION_WALLPAPERS.pathForMood[static_cast<size_t>(moodIndex)].clear();
      COMPANION_WALLPAPERS.saveToFile();
      requestUpdate();
    }
  });
  requestUpdate();
}

void CompanionWallpaperSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) swallowBackRelease = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (swallowBackRelease) {
      swallowBackRelease = false;
      return;
    }
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int moodCount = static_cast<int>(companion::MOOD_COUNT);

  switch (handleListTouch(selectedIndex, moodCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this, moodCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, moodCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, moodCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, moodCount);
    requestUpdate();
  });
}

void CompanionWallpaperSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_COMPANION_MOOD_WALLPAPERS), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int moodCount = static_cast<int>(companion::MOOD_COUNT);

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, moodCount, selectedIndex,
      [](int index) -> std::string { return std::string(companion::moodLabel(static_cast<companion::Mood>(index))); },
      nullptr, nullptr,
      [](int index) -> std::string {
        const auto& path = COMPANION_WALLPAPERS.pathForMood[static_cast<size_t>(index)];
        return path.empty() ? std::string(tr(STR_NOT_SET)) : baseName(path);
      },
      false, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
