#include "QuickPickActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// More headroom than Home's own companion column gets, since this screen has
// nothing else competing for space.
constexpr int MAX_SCALE = 6;
constexpr int PAD = 8;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
}  // namespace

void QuickPickActivity::onEnter() {
  Activity::onEnter();
  requestUpdate(true);
}

void QuickPickActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Nothing to jump to with an empty pool -- Confirm just leaves, same as Back.
    if (poolEmpty) {
      finish();
      return;
    }
    if (isHabit) {
      activityManager.goToHabits();
    } else {
      activityManager.goToTasks();
    }
  }
}

void QuickPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QUICK_PICK_TITLE),
                 nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int contentWidth = pageWidth - MARGIN * 2;
  const int textWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();

  const std::string text = poolEmpty ? std::string(tr(STR_QUICK_PICK_EMPTY)) : pickedText;
  std::vector<std::string> lines;
  if (renderer.getTextWidth(UI_10_FONT_ID, text.c_str()) <= textWidth) {
    lines.push_back(text);
  } else {
    lines = renderer.wrappedText(UI_10_FONT_ID, text.c_str(), textWidth, 4);
  }
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;

  // Whole-pixel scales only: fractional scaling would smear the baked dither.
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) > contentWidth) continue;
    if (bubbleBlock + companion::poseHeight(candidate) <= contentHeight) {
      scale = candidate;
      break;
    }
  }

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int blockH = bubbleBlock + spriteH;
  const int blockTop = contentTop + (contentHeight - blockH) / 2;
  const int centreX = pageWidth / 2;
  const int bubbleX = centreX - contentWidth / 2;

  companion::drawSpeechBubble(renderer, bubbleX, blockTop, contentWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom);
  const Rect textBounds{bubbleX + PAD, blockTop, textWidth, bubbleH};
  int textY = blockTop + PAD;
  for (const auto& line : lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
    textY += lineH;
  }

  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, blockTop + bubbleBlock, scale);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), poolEmpty ? "" : tr(STR_QUICK_PICK_GO), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
