#include "CompanionSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "companion/CompanionSprites.generated.h"
#include "fontIds.h"

namespace {
constexpr int ROW_ENABLED = 0;
constexpr int ROW_CHARACTER = 1;
constexpr int ROW_ON_HOME = 2;
constexpr int ROW_SHOW_MOOD_LABEL = 3;

// Name plus species, because a list of six bare proper nouns tells you nothing
// about what you are choosing. The popup cannot show sprites: its FreeInkUI
// DialogOption has no icon slot, and adding one would mean patching the SDK
// submodule and losing it on the next update.
std::vector<std::string> characterLabels() {
  std::vector<std::string> labels;
  labels.reserve(companion::COMPANION_COUNT);
  for (int i = 0; i < companion::COMPANION_COUNT; i++) {
    std::string label = companion::COMPANION_NAMES[i];
    label += " (";
    label += companion::COMPANION_KINDS[i];
    label += ")";
    labels.emplace_back(std::move(label));
  }
  return labels;
}
}  // namespace

void CompanionSettingsActivity::onEnter() {
  Activity::onEnter();
  if (SETTINGS.companionId >= companion::COMPANION_COUNT) SETTINGS.companionId = 0;
  selectedIndex = 0;
  requestUpdate();
}

void CompanionSettingsActivity::onExit() { Activity::onExit(); }

void CompanionSettingsActivity::handleSelection() {
  switch (selectedIndex) {
    case ROW_ENABLED:
      SETTINGS.companionEnabled = (SETTINGS.companionEnabled + 1) % 2;
      break;
    case ROW_CHARACTER:
      optionPopup.show(StrId::STR_COMPANION_CHARACTER, characterLabels(), SETTINGS.companionId, [this](int idx) {
        SETTINGS.companionId = static_cast<uint8_t>(idx);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      return;
    case ROW_ON_HOME:
      SETTINGS.companionOnHome = (SETTINGS.companionOnHome + 1) % 2;
      break;
    case ROW_SHOW_MOOD_LABEL:
      SETTINGS.companionShowMoodLabel = (SETTINGS.companionShowMoodLabel + 1) % 2;
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

void CompanionSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });
}

void CompanionSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_COMPANION), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const uint8_t characterIdx = SETTINGS.companionId < companion::COMPANION_COUNT ? SETTINGS.companionId : 0;
  const std::string characterValue =
      std::string(companion::COMPANION_NAMES[characterIdx]) + " (" + companion::COMPANION_KINDS[characterIdx] + ")";

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_ENABLED:
            return std::string(tr(STR_COMPANION_ENABLED));
          case ROW_CHARACTER:
            return std::string(tr(STR_COMPANION_CHARACTER));
          case ROW_ON_HOME:
            return std::string(tr(STR_COMPANION_ON_HOME));
          default:
            return std::string(tr(STR_COMPANION_SHOW_MOOD_LABEL));
        }
      },
      nullptr, nullptr,
      [&characterValue](int index) -> std::string {
        switch (index) {
          case ROW_ENABLED:
            return SETTINGS.companionEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_CHARACTER:
            return characterValue;
          case ROW_ON_HOME:
            return SETTINGS.companionOnHome ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          default:
            return SETTINGS.companionShowMoodLabel ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
