#include "CompanionSettingsActivity.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/CompanionSleepTimeActivity.h"
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
constexpr int ROW_SHOW_MOOD_LABEL = 1;
constexpr int ROW_MOOD_WALLPAPERS = 2;
constexpr int ROW_SLEEP_START = 3;
constexpr int ROW_SLEEP_END = 4;
constexpr int ROW_ACTIVE_FOR = 5;

// "HH:MM" for a Sleep start/end row's value column -- digits need no
// translation.
std::string sleepTimeValue(const uint8_t hour, const uint8_t minute) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
  return std::string(buf);
}

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
    case ROW_SLEEP_START:
    case ROW_SLEEP_END:
      startActivityForResult(
          std::make_unique<CompanionSleepTimeActivity>(renderer, mappedInput, selectedIndex == ROW_SLEEP_START),
          [this](const ActivityResult&) {
            // Same reasoning as ROW_MOOD_WALLPAPERS above.
            if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
              swallowConfirmRelease = true;
            }
            if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
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

  const std::string ageValue = formatAge(COMPANION_STATE.activatedDay);

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_ENABLED:
            return std::string(tr(STR_COMPANION_ENABLED));
          case ROW_SHOW_MOOD_LABEL:
            return std::string(tr(STR_COMPANION_SHOW_MOOD_LABEL));
          case ROW_MOOD_WALLPAPERS:
            return std::string(tr(STR_COMPANION_MOOD_WALLPAPERS));
          case ROW_SLEEP_START:
            return std::string(tr(STR_COMPANION_SLEEP_START));
          case ROW_SLEEP_END:
            return std::string(tr(STR_COMPANION_SLEEP_END));
          default:
            return std::string(tr(STR_COMPANION_ACTIVE_FOR));
        }
      },
      nullptr, nullptr,
      [&ageValue](int index) -> std::string {
        switch (index) {
          case ROW_ENABLED:
            return SETTINGS.companionEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_SHOW_MOOD_LABEL:
            return SETTINGS.companionShowMoodLabel ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ROW_MOOD_WALLPAPERS:
            return wallpaperCountValue();
          case ROW_SLEEP_START:
            return sleepTimeValue(SETTINGS.companionSleepStartHour, SETTINGS.companionSleepStartMinute);
          case ROW_SLEEP_END:
            return sleepTimeValue(SETTINGS.companionSleepEndHour, SETTINGS.companionSleepEndMinute);
          default:
            return ageValue;
        }
      },
      true, [](int index) -> bool { return index == ROW_ACTIVE_FOR; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
