#include "QuickPickActivity.h"

#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <I18n.h>
#include <TodoistTaskCache.h>

#include <memory>
#include <vector>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionTracker.h"
#include "companion/QuickPickRoll.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/OrganizerActions.h"

namespace {
// More headroom than Home's own companion column gets, since this screen has
// nothing else competing for space.
constexpr int MAX_SCALE = 6;
constexpr int PAD = 8;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;

// Same bounds HabitsActivity's own number entry uses -- see its own comment
// for why 50/1/5.
constexpr int MAX_HABIT_LOG_AMOUNT = 50;
constexpr int HABIT_LOG_SMALL_STEP = 1;
constexpr int HABIT_LOG_LARGE_STEP = 5;

// Mirrored into CrossPointState rather than fished out reactively when sleep
// happens: keeps whatever the screen is showing durable at all times it's
// up, and needs no RTTI to read back out of a generic Activity* later (this
// build has none). Guarded so re-entering with the exact pick already held
// (a resume from sleep, or a reroll landing back where it started) does not
// cost a redundant SD write.
void mirrorToAppState(const std::string& text, const std::string& itemId, const bool isHabit, const bool poolEmpty) {
  if (APP_STATE.quickPickText == text && APP_STATE.quickPickItemId == itemId && APP_STATE.quickPickIsHabit == isHabit &&
      APP_STATE.quickPickPoolEmpty == poolEmpty) {
    return;
  }
  APP_STATE.quickPickText = text;
  APP_STATE.quickPickItemId = itemId;
  APP_STATE.quickPickIsHabit = isHabit;
  APP_STATE.quickPickPoolEmpty = poolEmpty;
  APP_STATE.saveToFile();
}
}  // namespace

void QuickPickActivity::onEnter() {
  Activity::onEnter();
  mirrorToAppState(pickedText, itemId, isHabit, poolEmpty);
  requestUpdate(true);
}

void QuickPickActivity::reroll() {
  const auto rolled = quickpick::roll();
  pickedText = rolled.text;
  itemId = rolled.itemId;
  isHabit = rolled.isHabit;
  poolEmpty = rolled.poolEmpty;
  mirrorToAppState(pickedText, itemId, isHabit, poolEmpty);
  requestUpdate();
}

bool QuickPickActivity::currentPickStillEligible() const {
  if (poolEmpty) return false;
  if (isHabit) {
    for (const auto& habit : HABITIFY_HABITS.getHabits()) {
      if (habit.id == itemId) return !habit.isComplete();
    }
    return false;
  }
  for (const auto& task : TODOIST_TASKS.getTasks()) {
    if (task.id == itemId) return true;
  }
  return false;
}

void QuickPickActivity::showOptions() {
  std::vector<std::string> options{isHabit ? tr(STR_HABITIFY_LOG) : tr(STR_COMPLETE_TASK), tr(STR_FOCUS_SESSION)};
  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the
        // press, this screen on the release). Back is swallowed whenever the
        // result was cancelled at all, since dismissing the popup with Back
        // can itself be release-triggered - by then the button is no longer
        // down, but the release is still what this screen would see next.
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        if (std::get<OptionPickResult>(result.data).index != 0) return;  // Focus session: nothing yet.
        if (isHabit) {
          logSuggestedHabit();
        } else {
          completeSuggestedTask();
        }
      });
}

void QuickPickActivity::completeSuggestedTask() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already (completed/deleted since the pick was made)

  // Asked rather than done: completing pushes to Todoist and cannot be undone
  // from the device -- same prompt TasksActivity itself shows.
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                                                tasks[cacheIndex].content),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;

                           // Re-resolve: the popup sat on top for as long as the user took to answer.
                           const auto& tasks2 = TODOIST_TASKS.getTasks();
                           size_t idx = tasks2.size();
                           for (size_t i = 0; i < tasks2.size(); i++) {
                             if (tasks2[i].id == itemId) {
                               idx = i;
                               break;
                             }
                           }
                           if (idx < tasks2.size()) {
                             RenderLock lock(*this);
                             organizerActions::completeTask(idx);
                           }
                           if (!currentPickStillEligible()) reroll();
                         });
}

void QuickPickActivity::logSuggestedHabit() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already
  // A habit with no goal has no unit either, so there is nothing to log
  // against it -- same guard HabitsActivity's own row applies.
  if (habits[cacheIndex].unitSymbol.empty()) return;
  const auto& habit = habits[cacheIndex];

  startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                             renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                             MAX_HABIT_LOG_AMOUNT, HABIT_LOG_SMALL_STEP, HABIT_LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                             /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true, StrId::STR_NONE_OPT,
                             habit.name, habit.unitSymbol),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;

                           const auto amount = std::get<IntervalResult>(result.data).value;
                           const auto& habits2 = HABITIFY_HABITS.getHabits();
                           size_t idx = habits2.size();
                           for (size_t i = 0; i < habits2.size(); i++) {
                             if (habits2[i].id == itemId) {
                               idx = i;
                               break;
                             }
                           }
                           if (idx < habits2.size()) {
                             RenderLock lock(*this);
                             organizerActions::logHabit(idx, static_cast<float>(amount));
                           }
                           if (!currentPickStillEligible()) reroll();
                         });
}

void QuickPickActivity::loop() {
  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) swallowBackRelease = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (swallowBackRelease) {
      // The tail of the press that cancelled a popup pushed from this screen.
      // Acting on it would leave the screen entirely instead of just closing
      // the popup that press already closed.
      swallowBackRelease = false;
      return;
    }
    setResult(QuickPickResult{pickedText, itemId, isHabit, poolEmpty});
    finish();
    return;
  }

  if (!poolEmpty && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    reroll();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (swallowConfirmRelease) {
      // The tail of the press that answered a popup pushed from this screen.
      // Acting on it would reopen it, and cancelling would reopen it again.
      swallowConfirmRelease = false;
      return;
    }
    // Nothing to act on with an empty pool -- Confirm just leaves, same as Back.
    if (poolEmpty) {
      setResult(QuickPickResult{pickedText, itemId, isHabit, poolEmpty});
      finish();
      return;
    }
    showOptions();
  }
}

void QuickPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_COMPANION), nullptr);

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

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), poolEmpty ? "" : tr(STR_QUICK_PICK_GO), "",
                                            poolEmpty ? "" : tr(STR_QUICK_PICK_RANDOM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
