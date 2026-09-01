#include "QuickPickActivity.h"

#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <I18n.h>
#include <TodoistTaskCache.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "CrossPointState.h"
#include "LogsActivity.h"
#include "MappedInputManager.h"
#include "activities/organizer/RescheduleTaskActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionState.h"
#include "companion/CompanionTracker.h"
#include "companion/QuickPickRoll.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/OrganizerActions.h"

namespace {
// More headroom than Home's own companion column gets, since this screen has
// nothing else competing for space.
constexpr int MAX_SCALE = 6;
constexpr int PAD = 14;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
// Between the character and the two-column info block below it.
constexpr int LABEL_GAP = 20;
// Between adjacent rows within one column (mood/stats on the left, lifetime
// info on the right) -- both columns share this so their three rows land
// level with each other.
constexpr int ROW_GAP = 4;
// getTextHeight() reports the ascender only, but drawText() takes y as the
// top and descenders hang below it.
constexpr int DESCENDER_ALLOWANCE = 3;
// Floor on the bubble's text column, so a one-word habit name still leaves
// room for the tail and rounded corners rather than shrinking to fit it
// exactly.
constexpr int MIN_BUBBLE_TEXT_WIDTH = 80;

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
  // Same overdue-or-due-today rule quickpick::roll() itself filters on -
  // rescheduling can move a task out of the pool exactly as completing one
  // does, just without removing it from the cache.
  const bool knowToday = !TODOIST_TASKS.getSyncDate().empty();
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  for (const auto& task : TODOIST_TASKS.getTasks()) {
    if (task.id == itemId) return knowToday && (task.overdue || task.dueDays == today);
  }
  return false;
}

void QuickPickActivity::showOptions() {
  // A habit with no goal has no unit, so nothing can be logged against it (see
  // logSuggestedHabit()) - Log is left off the menu rather than shown and
  // silently failing. Complete needs no unit, so it is offered either way.
  bool canLog = true;
  if (isHabit) {
    for (const auto& habit : HABITIFY_HABITS.getHabits()) {
      if (habit.id == itemId) {
        canLog = !habit.unitSymbol.empty();
        break;
      }
    }
  }

  // Todoist has no way to reschedule a single occurrence of a recurring task
  // without replacing its recurrence entirely, and the warning that fact
  // requires doesn't fit the popup -- simplest and clearest is to just not
  // offer Reschedule for a recurring task at all.
  bool canReschedule = true;
  if (!isHabit) {
    for (const auto& task : TODOIST_TASKS.getTasks()) {
      if (task.id == itemId) {
        canReschedule = !task.isRecurring;
        break;
      }
    }
  }

  std::vector<std::string> options;
  if (isHabit) {
    if (canLog) options.push_back(tr(STR_HABITIFY_LOG));
    options.push_back(tr(STR_COMPLETE_HABIT));
    options.push_back(tr(STR_FOCUS_SESSION));
  } else {
    options.push_back(tr(STR_COMPLETE_TASK));
    options.push_back(tr(STR_FOCUS_SESSION));
    if (canReschedule) options.push_back(tr(STR_RESCHEDULE_TASK));
  }
  // Positions within the habit branch's own entries; the task branch's are
  // fixed (0/1/[2]) and never overlap with these, since the two branches are
  // mutually exclusive on isHabit.
  const int logIdx = canLog ? 0 : -1;
  const int completeHabitIdx = canLog ? 1 : 0;
  const int habitFocusIdx = canLog ? 2 : 1;
  const int rescheduleIdx = canReschedule ? 2 : -1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, logIdx, completeHabitIdx, habitFocusIdx, rescheduleIdx](const ActivityResult& result) {
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
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (isHabit) {
          if (idx == logIdx) {
            logSuggestedHabit();
          } else if (idx == completeHabitIdx) {
            completeSuggestedHabit();
          } else if (idx == habitFocusIdx) {
            offerFocusSession();
          }
        } else {
          if (idx == 0) {
            completeSuggestedTask();
          } else if (idx == 1) {
            offerFocusSession();
          } else if (idx == rescheduleIdx) {
            offerReschedule();
          }
        }
      });
}

void QuickPickActivity::offerFocusSession() {
  const std::string capturedText = pickedText;
  const std::string capturedItemId = itemId;
  const bool capturedIsHabit = isHabit;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, capturedText, capturedItemId, capturedIsHabit](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(capturedText, capturedItemId, capturedIsHabit,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
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

void QuickPickActivity::offerReschedule() {
  std::vector<std::string> options;
  options.push_back(tr(STR_PICK_DATE));
  options.push_back(tr(STR_TASKS_TAB_NO_DATE));

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_RESCHEDULE_TASK, std::move(options)),
      [this](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          offerRescheduleDatePicker();
        } else if (idx == 1) {
          clearTaskDueDate();
        }
      });
}

void QuickPickActivity::offerRescheduleDatePicker() {
  // Only ever reached for a non-recurring task -- showOptions() leaves
  // Reschedule off the menu entirely for a recurring one.
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already
  const uint16_t seed = tasks[cacheIndex].dueDays != todoist::DUE_NONE
                            ? tasks[cacheIndex].dueDays
                            : todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  startActivityForResult(std::make_unique<RescheduleTaskActivity>(renderer, mappedInput, seed),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const auto* date = std::get_if<DateResult>(&result.data);
                           if (!date) return;

                           // Re-resolve: the picker sat on top for as long as the user took to answer.
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
                             organizerActions::rescheduleTask(idx, date->packedDate);
                           }
                           if (!currentPickStillEligible()) reroll();
                         });
}

void QuickPickActivity::clearTaskDueDate() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already

  {
    RenderLock lock(*this);
    organizerActions::rescheduleTask(cacheIndex, todoist::DUE_NONE);
  }
  if (!currentPickStillEligible()) reroll();
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

void QuickPickActivity::completeSuggestedHabit() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already

  {
    RenderLock lock(*this);
    organizerActions::completeHabit(cacheIndex);
  }
  if (!currentPickStillEligible()) reroll();
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    startActivityForResult(std::make_unique<LogsActivity>(renderer, mappedInput), [](const ActivityResult&) {});
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
  const int maxTextWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();
  const char* label = companion::moodLabel(mood);

  // Left column: mood label (bold) plus today's completed tasks/habits as a
  // bulleted list (regular weight) underneath.
  char taskCount[24];
  const int tasksToday = static_cast<int>(TODOIST_TASKS.getCompletedToday());
  snprintf(taskCount, sizeof(taskCount), tasksToday == 1 ? tr(STR_COMPANION_STATS_TASK) : tr(STR_COMPANION_STATS_TASKS),
           tasksToday);
  char habitCount[24];
  const auto& habits = HABITIFY_HABITS.getHabits();
  const int habitsToday = static_cast<int>(
      std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.isComplete(); }));
  snprintf(habitCount, sizeof(habitCount),
           habitsToday == 1 ? tr(STR_COMPANION_STATS_HABIT) : tr(STR_COMPANION_STATS_HABITS), habitsToday);
  const std::string taskStatLine = std::string("\xE2\x80\xA2 ") + taskCount;
  const std::string habitStatLine = std::string("\xE2\x80\xA2 ") + habitCount;

  // Right column: lifetime info -- name, age, best-ever single-day score.
  // Each row is a bold label immediately followed by a regular value, so
  // every row needs two drawText() calls -- one call takes a single style
  // for the whole string.
  const std::string nameValue = companion::COMPANION_NAMES[static_cast<size_t>(id)];
  const std::string ageValue = CompanionTracker::formatAge(COMPANION_STATE.activatedDay);
  char highscoreBuf[8];
  snprintf(highscoreBuf, sizeof(highscoreBuf), "%u", COMPANION_STATE.ledger.bestDayPoints);

  const std::string nameBold = std::string(tr(STR_COMPANION_NAME)) + ":";
  const std::string nameRegular = " " + nameValue;
  const std::string ageBold = std::string(tr(STR_COMPANION_AGE)) + ":";
  const std::string ageRegular = " " + ageValue;
  const std::string highscoreBold = std::string(tr(STR_COMPANION_HIGHSCORE)) + ":";
  const std::string highscoreRegular = std::string(" ") + highscoreBuf;

  const std::string text = mood == companion::Mood::Sleeping ? std::string(tr(STR_COMPANION_SLEEPING_BUBBLE))
                           : poolEmpty                       ? std::string(tr(STR_QUICK_PICK_EMPTY))
                                                             : pickedText;
  const auto textFit = companion::fitBubbleText(renderer, UI_10_FONT_ID, text, maxTextWidth, MIN_BUBBLE_TEXT_WIDTH, 4);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(textFit.lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int bubbleWidth = textFit.textWidth + PAD * 2;

  // Both columns share one line height (same font, three rows each), so the
  // two land level with each other row for row.
  const int infoLineH = renderer.getTextHeight(UI_10_FONT_ID) + DESCENDER_ALLOWANCE;
  const int infoBlockH = infoLineH * 3 + ROW_GAP * 2;
  const int statusBlock = LABEL_GAP + infoBlockH;

  // Whole-pixel scales only: fractional scaling would smear the baked dither.
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) > contentWidth) continue;
    if (bubbleBlock + companion::poseHeight(candidate) + statusBlock <= contentHeight) {
      scale = candidate;
      break;
    }
  }

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int centreX = pageWidth / 2;
  const int bubbleX = centreX - bubbleWidth / 2;

  // Bubble + sprite + info centred as one assembly within the content area,
  // so the companion itself sits in the middle of the screen rather than
  // pinned to the top.
  const int totalBlockH = bubbleBlock + spriteH + statusBlock;
  const int blockTop = contentTop + std::max(0, (contentHeight - totalBlockH) / 2);

  companion::drawSpeechBubble(renderer, bubbleX, blockTop, bubbleWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom);
  const Rect textBounds{bubbleX + PAD, blockTop, textFit.textWidth, bubbleH};
  int textY = blockTop + PAD;
  for (const auto& line : textFit.lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
    textY += lineH;
  }

  const int spriteTop = blockTop + bubbleBlock;
  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, spriteTop, scale);

  // Two independent halves of the content width under the sprite -- mood+
  // stats on the left, lifetime info on the right -- each block centred
  // within its own half rather than the pair centred as one group, so one
  // column's width can never pull the other off its half's own centre.
  auto rowWidth = [&](const std::string& boldPart, const std::string& regularPart) {
    return renderer.getTextWidth(UI_10_FONT_ID, boldPart.c_str(), EpdFontFamily::BOLD) +
           renderer.getTextWidth(UI_10_FONT_ID, regularPart.c_str(), EpdFontFamily::REGULAR);
  };
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
  const int taskLineW = renderer.getTextWidth(UI_10_FONT_ID, taskStatLine.c_str(), EpdFontFamily::REGULAR);
  const int habitLineW = renderer.getTextWidth(UI_10_FONT_ID, habitStatLine.c_str(), EpdFontFamily::REGULAR);
  const int leftColumnW = std::max({labelW, taskLineW, habitLineW});

  const int nameRowW = rowWidth(nameBold, nameRegular);
  const int ageRowW = rowWidth(ageBold, ageRegular);
  const int highscoreRowW = rowWidth(highscoreBold, highscoreRegular);
  const int rightColumnW = std::max({nameRowW, ageRowW, highscoreRowW});

  const int halfWidth = contentWidth / 2;
  const int leftHalfCentreX = MARGIN + halfWidth / 2;
  const int rightHalfCentreX = centreX + halfWidth / 2;
  const int leftX = leftHalfCentreX - leftColumnW / 2;
  const int rightX = rightHalfCentreX - rightColumnW / 2;

  const int infoTop = spriteTop + spriteH + LABEL_GAP;
  const int row1Y = infoTop;
  const int row2Y = row1Y + infoLineH + ROW_GAP;
  const int row3Y = row2Y + infoLineH + ROW_GAP;

  renderer.drawText(UI_10_FONT_ID, leftX, row1Y, label, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, leftX, row2Y, taskStatLine.c_str(), true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, leftX, row3Y, habitStatLine.c_str(), true, EpdFontFamily::REGULAR);

  auto drawLabelledRow = [&](const int y, const std::string& boldPart, const std::string& regularPart) {
    renderer.drawText(UI_10_FONT_ID, rightX, y, boldPart.c_str(), true, EpdFontFamily::BOLD);
    const int boldW = renderer.getTextWidth(UI_10_FONT_ID, boldPart.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, rightX + boldW, y, regularPart.c_str(), true, EpdFontFamily::REGULAR);
  };
  drawLabelledRow(row1Y, nameBold, nameRegular);
  drawLabelledRow(row2Y, ageBold, ageRegular);
  drawLabelledRow(row3Y, highscoreBold, highscoreRegular);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), poolEmpty ? "" : tr(STR_QUICK_PICK_GO), tr(STR_LOGS),
                                            poolEmpty ? "" : tr(STR_QUICK_PICK_RANDOM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
