#include "BleNotificationDetailActivity.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BleNotificationDetailActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BleNotificationDetailActivity::loop() {
  // finish(), not onGoHome(): this is pushed on top of BleNotificationsActivity
  // (startActivityForResult), so either button just pops back to the list.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void BleNotificationDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char timeBuf[16] = "";
  HalClock::formatHourMinute(timeBuf, sizeof(timeBuf), hour, minute, SETTINGS.clockUtcOffsetQ,
                             SETTINGS.clockFormat == 1);
  const char* headerTitle = isCall ? tr(STR_BLE_INCOMING_CALL) : sender.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerTitle, timeBuf);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight;
  const Rect bounds{metrics.contentSidePadding, contentTop, pageWidth - metrics.contentSidePadding * 2, contentHeight};

  // "Title: content", same as the list row, but here nothing is truncated --
  // this screen exists specifically to show what the row cut off.
  std::string full = title;
  if (!content.empty()) {
    full += ": ";
    full += content;
  }
  constexpr int MAX_LINES = 12;  // A whole content area's worth on this panel's fonts; excess still just clips.
  UITheme::drawCenteredWrappedText(renderer, bounds, UI_10_FONT_ID, full.c_str(), MAX_LINES, true,
                                   EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // ENABLE_BLE_NOTIFY_SPIKE
