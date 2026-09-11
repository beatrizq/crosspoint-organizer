#include "BleNotificationsActivity.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "BleNotificationDetailActivity.h"
#include "BleNotificationQueue.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Hold threshold for the long-press Dismiss action (firmware convention, same
// value RecentBooksActivity's long-press-to-remove uses).
constexpr unsigned long LONG_PRESS_MS = 1000;

// Same font selection as OrganizerScreenActivity's titleFontId()/
// subtitleFontId() (Tasks/Calendar/Budget/Habits) -- not inherited from
// there, since that base class also brings tabs and an unconditional
// WiFi-teardown reboot-on-exit this screen has no use for, but the user
// asked for the same reading experience, so the two font choices are kept in
// lockstep with those screens' own logic.
int titleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? UI_10_FONT_ID : UI_12_FONT_ID;
}

int subtitleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? SMALL_FONT_ID : UI_10_FONT_ID;
}

// Same dither-overlay technique as OrganizerScreenActivity::dimText(): this
// e-ink panel has no real greyscale, so "dimmed" text is solid text with a
// checkerboard of pixels punched back out over it. Call after drawText() at
// the same position.
void dimText(const GfxRenderer& renderer, const int x, const int y, const int fontId, const char* text,
             const bool ink) {
  if (!ink || text == nullptr || text[0] == '\0') return;
  const int width = renderer.getTextWidth(fontId, text);
  const int height = renderer.getLineHeight(fontId);
  for (int py = y; py < y + height; py++) {
    for (int px = x; px < x + width; px++) {
      if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }
  }
}

constexpr int SEPARATOR_HEIGHT = 2;

}  // namespace

void BleNotificationsActivity::openDetail() {
  const BleNotificationEntry& e = BLE_NOTIFICATIONS.getEntry(selectorIndex);
  startActivityForResult(std::make_unique<BleNotificationDetailActivity>(renderer, mappedInput, e.sender, e.title,
                                                                         e.content, e.isCall, e.hour, e.minute),
                         [](const ActivityResult&) {});
}

void BleNotificationsActivity::dismissAll() {
  BLE_NOTIFICATIONS.clearAll();
  BLE_NOTIFICATIONS.saveToFile();
  selectorIndex = 0;
  requestUpdate(true);
}

void BleNotificationsActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  // Seen it -- clears the Home badge. Saved right away so the clear survives
  // a WiFi-sync reboot the same way the entries themselves do.
  if (BLE_NOTIFICATIONS.getUnreadCount() > 0) {
    BLE_NOTIFICATIONS.markAllRead();
    BLE_NOTIFICATIONS.saveToFile();
  }
  requestUpdate();
}

void BleNotificationsActivity::loop() {
  const int itemCount = static_cast<int>(BLE_NOTIFICATIONS.getCount());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  // After a long-press has fired, swallow input until Confirm is physically
  // released (so the release doesn't also open the detail view; re-arm only
  // once the button is up) -- same pattern RecentBooksActivity uses for its
  // own long-press action.
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longPressFired = false;
    }
    return;
  }

  // Long-press Confirm: Dismiss (clear the whole queue). Fires when the hold
  // times out while still held.
  if (itemCount > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    dismissAll();
    return;
  }

  // Plain Confirm: Select -- open the full-detail view of the selected entry.
  if (itemCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openDetail();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = renderer.getLineHeight(titleFontId()) + renderer.getLineHeight(subtitleFontId()) +
                        std::max(6, renderer.getLineHeight(titleFontId()) * 2 / 5);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int pageItems = std::max(1, contentHeight / std::max(1, rowHeight));

  int touchSel = static_cast<int>(selectorIndex);
  const auto listTouch = handleListTouch(touchSel, itemCount, contentTop, contentHeight, true);
  if (listTouch != ListTouchResult::None) {
    selectorIndex = static_cast<size_t>(touchSel);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), itemCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), itemCount);
    requestUpdate();
  });
}

void BleNotificationsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_NOTIFICATIONS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const size_t count = BLE_NOTIFICATIONS.getCount();
  if (count == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_BLE_NO_NOTIFICATIONS));
  } else {
    const int titleFont = titleFontId();
    const int subtitleFont = subtitleFontId();
    const int rowPad = std::max(6, renderer.getLineHeight(titleFont) * 2 / 5);
    const int rowHeight = renderer.getLineHeight(titleFont) + renderer.getLineHeight(subtitleFont) + rowPad;
    const int pageItems = std::max(1, contentHeight / std::max(1, rowHeight));
    const int textX = metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;
    const int itemCount = static_cast<int>(count);
    const int pageStart =
        static_cast<int>(selectorIndex) < 0 ? 0 : (static_cast<int>(selectorIndex) / pageItems) * pageItems;

    for (int row = 0; row < pageItems; row++) {
      const int index = pageStart + row;
      if (index >= itemCount) break;
      const int rowY = contentTop + row * rowHeight;
      const bool selected = index == static_cast<int>(selectorIndex);
      const bool ink = !selected;

      if (selected) {
        renderer.fillRect(0, rowY, pageWidth, rowHeight);
      }

      const BleNotificationEntry& e = BLE_NOTIFICATIONS.getEntry(static_cast<size_t>(index));

      char timeBuf[16] = "";
      HalClock::formatHourMinute(timeBuf, sizeof(timeBuf), e.hour, e.minute, SETTINGS.clockUtcOffsetQ,
                                 SETTINGS.clockFormat == 1);
      const int timeWidth = renderer.getTextWidth(titleFont, timeBuf);
      constexpr int TIME_GAP = 10;
      const int mainWidth = textWidth - timeWidth - TIME_GAP;

      // "Title: content", or just the title when there is no content (e.g. an
      // incoming call with no caller name -- see BleNotificationQueue's own
      // field comments). Sized from the struct's own field sizes rather than
      // a duplicated constant, so it can never fall short if those change.
      char mainLine[sizeof(e.title) + sizeof(": ") - 1 + sizeof(e.content)];
      if (e.content[0] != '\0') {
        snprintf(mainLine, sizeof(mainLine), "%s: %s", e.title, e.content);
      } else {
        snprintf(mainLine, sizeof(mainLine), "%s", e.title);
      }

      const int textY = rowY + rowPad / 2;
      const auto shownMain = renderer.truncatedText(titleFont, mainLine, mainWidth);
      renderer.drawText(titleFont, textX, textY, shownMain.c_str(), ink);
      renderer.drawText(titleFont, textX + mainWidth + TIME_GAP, textY, timeBuf, ink);

      // Sender/app name, dimmed so it stays subordinate to the message it
      // belongs to -- same treatment TasksActivity gives a task's due date.
      const int subY = textY + renderer.getLineHeight(titleFont);
      const auto shownSender = renderer.truncatedText(subtitleFont, e.sender, textWidth);
      renderer.drawText(subtitleFont, textX, subY, shownSender.c_str(), ink);
      dimText(renderer, textX, subY, subtitleFont, shownSender.c_str(), ink);

      // Dithered separator, skipped either side of the selected row (its fill
      // already bounds it) and after the last row on the page.
      const bool nextSelected = (index + 1) == static_cast<int>(selectorIndex);
      const bool lastOnPage = row + 1 >= pageItems || index + 1 >= itemCount;
      if (!selected && !nextSelected && !lastOnPage) {
        renderer.fillRectDither(textX, rowY + rowHeight - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                                Color::LightGray);
      }
    }
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_HOME), count > 0 ? tr(STR_SELECT) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // ENABLE_BLE_NOTIFY_SPIKE
