#include "TodoistSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_TOKEN = 0;
constexpr int ROW_SLEEP_SCREEN = 1;
constexpr int ROW_CLEAR = 2;
constexpr int ROW_HINT = 3;
}  // namespace

void TodoistSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void TodoistSettingsActivity::onExit() { Activity::onExit(); }

void TodoistSettingsActivity::loop() {
  auto activateSelected = [this] { handleSelection(); };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      activateSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, MENU_ITEMS, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, MENU_ITEMS, pageItems);
    requestUpdate();
    return;
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

void TodoistSettingsActivity::handleSelection() {
  if (selectedIndex == ROW_TOKEN) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODOIST_ENTER_TOKEN),
                                                                   TODOIST_STORE.getToken(),
                                                                   TodoistStore::MAX_TOKEN_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             TODOIST_STORE.setToken(kb.text);
                             TODOIST_STORE.saveToFile();
                             LOG_DBG("TDS", "API token %s", TODOIST_STORE.hasToken() ? "set" : "cleared");
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_SLEEP_SCREEN) {
    // Off keeps /sleep.bmp (and the sleep screen mode) as the user set them.
    TODOIST_STORE.setSleepScreenEnabled(!TODOIST_STORE.getSleepScreenEnabled());
    TODOIST_STORE.saveToFile();
    requestUpdate(true);
    return;
  }

  if (selectedIndex == ROW_CLEAR) {
    TODOIST_STORE.clearToken();
    TODOIST_STORE.saveToFile();
    LOG_DBG("TDS", "API token cleared");
    requestUpdate(true);
  }
  // ROW_HINT is a footnote, not an action.
}

void TodoistSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODOIST));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  // Live values; the token itself is never shown, only whether one is stored.
  const std::string tokenValue =
      TODOIST_STORE.hasToken() ? std::string("******") : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
  const std::string sleepScreenValue = std::string(
      I18n::getInstance().get(TODOIST_STORE.getSleepScreenEnabled() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_TOKEN:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_API_TOKEN));
          case ROW_SLEEP_SCREEN:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_SLEEP_SCREEN));
          case ROW_CLEAR:
            return std::string(I18n::getInstance().get(StrId::STR_CLEAR_BUTTON));
          default:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_HOLD_TO_SYNC));
        }
      },
      nullptr, nullptr,
      [&tokenValue, &sleepScreenValue](int index) -> std::string {
        if (index == ROW_TOKEN) return tokenValue;
        if (index == ROW_SLEEP_SCREEN) return sleepScreenValue;
        return std::string("");
      },
      false, [](int index) -> bool { return index == ROW_HINT; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
