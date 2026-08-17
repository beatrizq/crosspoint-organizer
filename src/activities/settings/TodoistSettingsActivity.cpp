#include "TodoistSettingsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>

#include <memory>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int ROW_TOKEN = 0;
constexpr int ROW_SLEEP_SCREEN = 1;
constexpr int ROW_CLEAR = 2;
constexpr int ROW_HINT = 3;
}  // namespace

TodoistSettingsActivity::TodoistSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TodoistSettings", renderer, mappedInput) {
  rowItems_[ROW_TOKEN].label = I18N.get(StrId::STR_TODOIST_API_TOKEN);
  rowItems_[ROW_SLEEP_SCREEN].label = I18N.get(StrId::STR_TODOIST_SLEEP_SCREEN);
  rowItems_[ROW_CLEAR].label = I18N.get(StrId::STR_CLEAR_BUTTON);
  // Non-interactive footnote: syncing lives on the Today screen, not here.
  rowItems_[ROW_HINT].label = I18N.get(StrId::STR_TODOIST_HOLD_TO_SYNC);
  rowItems_[ROW_HINT].isHeader = true;
  rowItems_[ROW_HINT].enabled = false;
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* TodoistSettingsActivity::headerTitle() const { return tr(STR_TODOIST); }

void TodoistSettingsActivity::activateIndex(const int index) {
  // Activation opens the keyboard or repaints a new value; a lingering flash
  // would gray an unrelated row.
  app.clearTapFlash();

  if (index == ROW_TOKEN) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODOIST_ENTER_TOKEN),
                                                                   TODOIST_STORE.getToken(),
                                                                   TodoistStore::MAX_TOKEN_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             TODOIST_STORE.setToken(kb.text);
                             TODOIST_STORE.saveToFile();
                             LOG_DBG("TDS", "API token %s", TODOIST_STORE.hasToken() ? "set" : "cleared");
                           });
    return;
  }

  if (index == ROW_SLEEP_SCREEN) {
    // Off keeps /sleep.bmp (and the sleep screen mode) as the user set them.
    TODOIST_STORE.setSleepScreenEnabled(!TODOIST_STORE.getSleepScreenEnabled());
    TODOIST_STORE.saveToFile();
    requestUpdate(true);
    return;
  }

  if (index == ROW_CLEAR) {
    TODOIST_STORE.clearToken();
    TODOIST_STORE.saveToFile();
    LOG_DBG("TDS", "API token cleared");
    requestUpdate(true);
  }
}

void TodoistSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Live values; the token itself is never shown.
  tokenValue_ = TODOIST_STORE.hasToken() ? "******" : tr(STR_NOT_SET);
  rowItems_[ROW_TOKEN].value = tokenValue_.c_str();
  sleepScreenValue_ = TODOIST_STORE.getSleepScreenEnabled() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowItems_[ROW_SLEEP_SCREEN].value = sleepScreenValue_.c_str();

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}
