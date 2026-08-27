#include "CompanionSleepTimeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint8_t MAX_HOURS = 23;
constexpr uint8_t MAX_MINUTES = 59;
constexpr int TOUCH_BUTTON_SIZE = 44;
constexpr int TOUCH_BUTTON_GAP = 18;

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}
}  // namespace

void CompanionSleepTimeActivity::onEnter() {
  Activity::onEnter();
  loadFromSettings();
  activeField = FIELD_HOURS;
  requestUpdate();
}

void CompanionSleepTimeActivity::onExit() {
  saveToSettings();
  Activity::onExit();
}

void CompanionSleepTimeActivity::loadFromSettings() {
  hours = isStart ? SETTINGS.companionSleepStartHour : SETTINGS.companionSleepEndHour;
  minutes = isStart ? SETTINGS.companionSleepStartMinute : SETTINGS.companionSleepEndMinute;
  if (hours > MAX_HOURS) hours = MAX_HOURS;
  if (minutes > MAX_MINUTES) minutes = MAX_MINUTES;
}

void CompanionSleepTimeActivity::saveToSettings() const {
  uint8_t& hourField = isStart ? SETTINGS.companionSleepStartHour : SETTINGS.companionSleepEndHour;
  uint8_t& minuteField = isStart ? SETTINGS.companionSleepStartMinute : SETTINGS.companionSleepEndMinute;
  if (hourField == hours && minuteField == minutes) return;
  hourField = hours;
  minuteField = minutes;
  SETTINGS.saveToFile();
}

void CompanionSleepTimeActivity::adjustActiveField(const int delta) {
  switch (activeField) {
    case FIELD_HOURS: {
      const int next = (static_cast<int>(hours) + delta + (MAX_HOURS + 1)) % (MAX_HOURS + 1);
      hours = static_cast<uint8_t>(next);
      break;
    }
    case FIELD_MINUTES: {
      const int next = (static_cast<int>(minutes) + delta + (MAX_MINUTES + 1)) % (MAX_MINUTES + 1);
      minutes = static_cast<uint8_t>(next);
      break;
    }
    default:
      break;
  }
}

bool CompanionSleepTimeActivity::fieldFromPoint(const int x, const int y, Field& field) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int colonGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;

  const int hoursBoxW = std::max(widthOf("00"), widthOf("23")) + fieldPaddingX * 2;
  const int colonWidth = widthOf(":");
  const int minutesBoxW = std::max(widthOf("00"), widthOf("59")) + fieldPaddingX * 2;
  const int totalWidth = hoursBoxW + colonGap + colonWidth + colonGap + minutesBoxW;

  int boxX = (pageWidth - totalWidth) / 2;
  auto hit = [&](const int width) {
    return x >= boxX && x < boxX + width && y >= centreY && y < centreY + fieldHeight;
  };
  if (hit(hoursBoxW)) {
    field = FIELD_HOURS;
    return true;
  }
  boxX += hoursBoxW + colonGap + colonWidth + colonGap;
  if (hit(minutesBoxW)) {
    field = FIELD_MINUTES;
    return true;
  }
  return false;
}

void CompanionSleepTimeActivity::getTouchControlRects(Rect& minusRect, Rect& plusRect) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int colonGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;
  const int hoursBoxW = std::max(widthOf("00"), widthOf("23")) + fieldPaddingX * 2;
  const int colonWidth = widthOf(":");
  const int minutesBoxW = std::max(widthOf("00"), widthOf("59")) + fieldPaddingX * 2;
  const int totalWidth = hoursBoxW + colonGap + colonWidth + colonGap + minutesBoxW;
  const int offsetX = (pageWidth - totalWidth) / 2;
  const int buttonY = centreY + (fieldHeight - TOUCH_BUTTON_SIZE) / 2;
  minusRect = Rect{offsetX - TOUCH_BUTTON_GAP - TOUCH_BUTTON_SIZE, buttonY, TOUCH_BUTTON_SIZE, TOUCH_BUTTON_SIZE};
  plusRect = Rect{offsetX + totalWidth + TOUCH_BUTTON_GAP, buttonY, TOUCH_BUTTON_SIZE, TOUCH_BUTTON_SIZE};
}

void CompanionSleepTimeActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activeField = static_cast<Field>((activeField + 1) % FIELD_COUNT);
    requestUpdate();
    return;
  }

  if (mappedInput.hasTouch()) {
    int tx = 0;
    int ty = 0;
    Rect minusRect;
    Rect plusRect;
    getTouchControlRects(minusRect, plusRect);

    if (mappedInput.wasScreenTouchDown(tx, ty)) {
      if (contains(minusRect, tx, ty) || contains(plusRect, tx, ty)) {
        return;
      }
      Field touchedField = FIELD_HOURS;
      if (fieldFromPoint(tx, ty, touchedField)) {
        if (activeField != touchedField) {
          activeField = touchedField;
          requestUpdate();
        }
        return;
      }
    }

    if (mappedInput.wasScreenTapped(tx, ty)) {
      if (contains(minusRect, tx, ty)) {
        adjustActiveField(-1);
        requestUpdate();
        return;
      }
      if (contains(plusRect, tx, ty)) {
        adjustActiveField(+1);
        requestUpdate();
        return;
      }

      Field touchedField = FIELD_HOURS;
      if (fieldFromPoint(tx, ty, touchedField)) {
        activeField = touchedField;
        requestUpdate();
        return;
      }
    }
  }

  buttonNavigator.onNextRelease([this] {
    adjustActiveField(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    adjustActiveField(-1);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    adjustActiveField(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    adjustActiveField(-1);
    requestUpdate();
  });
}

void CompanionSleepTimeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 isStart ? tr(STR_COMPANION_SLEEP_START) : tr(STR_COMPANION_SLEEP_END));

  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int colonGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;

  char hoursStr[8];
  snprintf(hoursStr, sizeof(hoursStr), "%02d", hours);
  char minutesStr[8];
  snprintf(minutesStr, sizeof(minutesStr), "%02d", minutes);

  const int hoursBoxW = std::max(widthOf("00"), widthOf("23")) + fieldPaddingX * 2;
  const int colonWidth = widthOf(":");
  const int minutesBoxW = std::max(widthOf("00"), widthOf("59")) + fieldPaddingX * 2;
  const int totalWidth = hoursBoxW + colonGap + colonWidth + colonGap + minutesBoxW;

  int x = (pageWidth - totalWidth) / 2;

  auto drawField = [&](const char* text, const int boxX, const int boxWidth, const Field field) {
    const bool selected = activeField == field;
    renderer.fillRectDither(boxX, centreY, boxWidth, fieldHeight, selected ? Color::LightGray : Color::White);
    renderer.drawRect(boxX, centreY, boxWidth, fieldHeight, true);
    if (selected) {
      renderer.drawRect(boxX + 1, centreY + 1, boxWidth - 2, fieldHeight - 2, true);
    }
    const int textX = boxX + (boxWidth - widthOf(text)) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, centreY, text, true, EpdFontFamily::BOLD);
  };

  drawField(hoursStr, x, hoursBoxW, FIELD_HOURS);
  x += hoursBoxW + colonGap;

  renderer.drawText(UI_12_FONT_ID, x, centreY, ":", true, EpdFontFamily::BOLD);
  x += colonWidth + colonGap;

  drawField(minutesStr, x, minutesBoxW, FIELD_MINUTES);

  if (mappedInput.hasTouch()) {
    Rect minusRect;
    Rect plusRect;
    getTouchControlRects(minusRect, plusRect);
    auto drawTouchButton = [&](const Rect& rect, const char* label) {
      renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::White);
      renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
      const int textX = rect.x + (rect.width - widthOf(label)) / 2;
      const int textY = rect.y + (rect.height - lineHeight) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
    };
    drawTouchButton(minusRect, "-");
    drawTouchButton(plusRect, "+");
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
