#include "LogsActivity.h"

#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <I18n.h>
#include <TodoistTaskCache.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"

namespace {

// Same font selection as OrganizerScreenActivity's titleFontId()/
// subtitleFontId() (Tasks/Calendar/Budget/Habits) -- not inherited from
// there directly (see this file's own header comment), but kept in lockstep
// so a completed-today log reads like the screens it is summarizing.
int titleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? UI_10_FONT_ID : UI_12_FONT_ID;
}

int subtitleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? SMALL_FONT_ID : UI_10_FONT_ID;
}

// Same dither-overlay technique as OrganizerScreenActivity::dimText() /
// BleNotificationsActivity's own copy of it: this e-ink panel has no real
// greyscale, so "dimmed" text is solid text with a checkerboard of pixels
// punched back out over it. Call after drawText() at the same position.
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

void LogsActivity::loadEntries() {
  entries.clear();
  const auto& titles = TODOIST_TASKS.getCompletedTodayTitles();
  entries.reserve(titles.size() + HABITIFY_HABITS.getHabits().size());
  for (const auto& title : titles) entries.push_back({title, false});
  for (const auto& habit : HABITIFY_HABITS.getHabits()) {
    if (habit.isComplete()) entries.push_back({habit.name, true});
  }
}

void LogsActivity::onEnter() {
  Activity::onEnter();
  loadEntries();
  selectorIndex = 0;
  requestUpdate();
}

void LogsActivity::onExit() {
  Activity::onExit();
  entries.clear();
}

void LogsActivity::loop() {
  const int itemCount = static_cast<int>(entries.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
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

void LogsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LOGS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_LOG_EMPTY));
  } else {
    const int titleFont = titleFontId();
    const int subtitleFont = subtitleFontId();
    const int rowPad = std::max(6, renderer.getLineHeight(titleFont) * 2 / 5);
    const int rowHeight = renderer.getLineHeight(titleFont) + renderer.getLineHeight(subtitleFont) + rowPad;
    const int pageItems = std::max(1, contentHeight / std::max(1, rowHeight));
    const int textX = metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;
    const int itemCount = static_cast<int>(entries.size());
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

      const Entry& e = entries[static_cast<size_t>(index)];
      const char* source =
          homeAppOrder::displayName(e.isHabit ? homeAppOrder::AppId::Habits : homeAppOrder::AppId::Tasks);

      const int textY = rowY + rowPad / 2;
      const auto shownTitle = renderer.truncatedText(titleFont, e.title.c_str(), textWidth);
      renderer.drawText(titleFont, textX, textY, shownTitle.c_str(), ink);

      // Which app it came from, dimmed so it stays subordinate to the item
      // itself -- same treatment TasksActivity gives a task's due date.
      const int subY = textY + renderer.getLineHeight(titleFont);
      const auto shownSource = renderer.truncatedText(subtitleFont, source, textWidth);
      renderer.drawText(subtitleFont, textX, subY, shownSource.c_str(), ink);
      dimText(renderer, textX, subY, subtitleFont, shownSource.c_str(), ink);

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

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
