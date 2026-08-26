#include "CompanionSettingsActivity.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/CompanionWallpaperSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "companion/CompanionSprites.generated.h"
#include "companion/CompanionState.h"
#include "companion/CompanionTracker.h"
#include "companion/CompanionWallpaperStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_ENABLED = 0;
constexpr int ROW_CHARACTER = 1;
constexpr int ROW_ON_HOME = 2;
constexpr int ROW_SHOW_MOOD_LABEL = 3;
constexpr int ROW_MOOD_WALLPAPERS = 4;
constexpr int ROW_ACTIVE_FOR = 5;

// "N/5" set, for the Mood Wallpapers row's value column -- digits need no
// translation, so this skips adding a format string just for them.
std::string wallpaperCountValue() {
  const auto* begin = COMPANION_WALLPAPERS.pathForMood;
  const auto* end = begin + companion::MOOD_COUNT;
  const auto count = std::count_if(begin, end, [](const std::string& path) { return !path.empty(); });
  char buf[8];
  snprintf(buf, sizeof(buf), "%d/%d", static_cast<int>(count), static_cast<int>(companion::MOOD_COUNT));
  return std::string(buf);
}

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

// "Active for" value: days while under a month old, then months, then years --
// each rounded down, since a settings row has no room for a full breakdown.
std::string formatAge(const int32_t activatedDay) {
  if (activatedDay == companion::DayLedger::NEVER) return std::string(tr(STR_COMPANION_AGE_NOT_YET));

  int32_t today = 0;
  if (!CompanionTracker::resolveLocalDay(today)) return std::string(tr(STR_COMPANION_AGE_NOT_YET));

  // A clock correction could put "today" before the stamped day; floor at 0
  // rather than showing a negative age.
  const int32_t elapsed = today > activatedDay ? today - activatedDay : 0;
  char buf[32];
  if (elapsed < 1) return std::string(tr(STR_COMPANION_AGE_TODAY));
  if (elapsed < 30) {
    snprintf(buf, sizeof(buf), elapsed == 1 ? tr(STR_COMPANION_AGE_DAY) : tr(STR_COMPANION_AGE_DAYS), elapsed);
    return std::string(buf);
  }
  if (elapsed < 365) {
    const int32_t months = elapsed / 30;
    snprintf(buf, sizeof(buf), months == 1 ? tr(STR_COMPANION_AGE_MONTH) : tr(STR_COMPANION_AGE_MONTHS), months);
    return std::string(buf);
  }
  const int32_t years = elapsed / 365;
  snprintf(buf, sizeof(buf), years == 1 ? tr(STR_COMPANION_AGE_YEAR) : tr(STR_COMPANION_AGE_YEARS), years);
  return std::string(buf);
}
}  // namespace

void CompanionSettingsActivity::onEnter() {
  Activity::onEnter();
  if (SETTINGS.companionId >= companion::COMPANION_COUNT) SETTINGS.companionId = 0;
  selectedIndex = 0;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  stampActivationIfNeeded();
  requestUpdate();
}

void CompanionSettingsActivity::onExit() { Activity::onExit(); }

void CompanionSettingsActivity::stampActivationIfNeeded() {
  if (!SETTINGS.companionEnabled) return;
  if (COMPANION_STATE.activatedDay != companion::DayLedger::NEVER) return;
  int32_t today = 0;
  if (!CompanionTracker::resolveLocalDay(today)) return;
  COMPANION_STATE.activatedDay = today;
  COMPANION_STATE.saveToFile();
}

void CompanionSettingsActivity::handleSelection() {
  switch (selectedIndex) {
    case ROW_ENABLED: {
      const bool turningOn = SETTINGS.companionEnabled == 0;
      startActivityForResult(std::make_unique<ConfirmationActivity>(
                                 renderer, mappedInput,
                                 turningOn ? tr(STR_COMPANION_ENABLE_CONFIRM) : tr(STR_COMPANION_DISABLE_CONFIRM), ""),
                             [this](const ActivityResult& result) {
                               // The popup answers on the button going down, so its release lands
                               // back here once this screen is active again -- same reasoning as
                               // swallowConfirmRelease in onEnter(), just armed from this result
                               // instead.
                               if (mappedInput.isPressed(MappedInputManager::Button::Confirm))
                                 swallowConfirmRelease = true;
                               if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                                 swallowBackRelease = true;
                               }
                               if (result.isCancelled) return;
                               SETTINGS.companionEnabled = (SETTINGS.companionEnabled + 1) % 2;
                               stampActivationIfNeeded();
                               SETTINGS.saveToFile();
                               requestUpdate();
                             });
      return;
    }
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
    case ROW_MOOD_WALLPAPERS:
      startActivityForResult(std::make_unique<CompanionWallpaperSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               // Same reasoning as ROW_ENABLED's popup above: this
                               // sub-screen answers Back/Confirm on the button going
                               // down in places, so a release can still land here.
                               if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                                 swallowConfirmRelease = true;
                               }
                               if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                                 swallowBackRelease = true;
                               }
                               requestUpdate();
                             });
      return;
    default:
      // ROW_ACTIVE_FOR is a read-only info row.
      return;
  }
  SETTINGS.saveToFile();
}

void CompanionSettingsActivity::loop() {
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
  const std::string ageValue = formatAge(COMPANION_STATE.activatedDay);

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
          case ROW_SHOW_MOOD_LABEL:
            return std::string(tr(STR_COMPANION_SHOW_MOOD_LABEL));
          case ROW_MOOD_WALLPAPERS:
            return std::string(tr(STR_COMPANION_MOOD_WALLPAPERS));
          default:
            return std::string(tr(STR_COMPANION_ACTIVE_FOR));
        }
      },
      nullptr, nullptr,
      [&characterValue, &ageValue](int index) -> std::string {
        switch (index) {
          case ROW_ENABLED:
            return SETTINGS.companionEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_CHARACTER:
            return characterValue;
          case ROW_ON_HOME:
            return SETTINGS.companionOnHome ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_SHOW_MOOD_LABEL:
            return SETTINGS.companionShowMoodLabel ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_MOOD_WALLPAPERS:
            return wallpaperCountValue();
          default:
            return ageValue;
        }
      },
      true, [](int index) -> bool { return index == ROW_ACTIVE_FOR; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
