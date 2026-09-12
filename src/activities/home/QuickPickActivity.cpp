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
// Smaller than the companion's own full-screen hero used to be (was 6):
// this screen now shares its height with a tab bar above and a row list
// below, so the figure gives up some of the room it used to have to itself.
constexpr int MAX_SCALE = 4;
constexpr int PAD = 14;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
// Between the sprite and whatever the active tab draws below it.
constexpr int LABEL_GAP = 20;
// Between the Logs tab's own mood label and the log list under it.
constexpr int ROW_GAP = 4;
// Floor on the bubble's text column, so a one-word habit name still leaves
// room for the tail and rounded corners rather than shrinking to fit it
// exactly.
constexpr int MIN_BUBBLE_TEXT_WIDTH = 80;
// The companion figure + bubble's own share of the space below the tab bar;
// the active tab's own content gets the rest. Same size regardless of tab.
constexpr int COMPANION_BUDGET_PERCENT = 45;

// Same bounds HabitsActivity's own number entry uses -- see its own comment
// for why 50/1/5.
constexpr int MAX_HABIT_LOG_AMOUNT = 50;
constexpr int HABIT_LOG_SMALL_STEP = 1;
constexpr int HABIT_LOG_LARGE_STEP = 5;

// Row list geometry shared by the Tasks/Habits tabs -- deliberately plain
// (title, one line, selected row inverted) rather than each real screen's
// own richer row: this is a glanceable subset, not a replacement for it. The
// Logs tab's own log list reuses the same row height, for the one line of
// title text it shows too, but never a selection fill -- nothing is
// selectable in it, so it also reuses the dithered separator the other two
// tabs have no need for (their selection fill already bounds every row).
constexpr int ROW_HEIGHT = 32;
constexpr int SEPARATOR_HEIGHT = 2;

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
  activeTab = Tab::Tasks;
  taskSelectedRow = 0;
  habitSelectedRow = 0;
  requestUpdate(true);
}

void QuickPickActivity::switchTab(const Tab next) {
  if (activeTab == next) return;
  activeTab = next;
  taskSelectedRow = 0;
  habitSelectedRow = 0;
}

std::vector<size_t> QuickPickActivity::relevantTaskIndices() const {
  // Same overdue-or-due-today rule quickpick::roll() itself pools from (see
  // its own comment) -- these are exactly the tasks a Random reroll could
  // land on, and completing/rescheduling one here credits the companion the
  // same way acting on the Logs tab's own suggestion does.
  std::vector<size_t> indices;
  const auto& tasks = TODOIST_TASKS.getTasks();
  const bool knowToday = !TODOIST_TASKS.getSyncDate().empty();
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  indices.reserve(tasks.size());
  for (size_t i = 0; i < tasks.size(); i++) {
    if (knowToday && (tasks[i].overdue || tasks[i].dueDays == today)) indices.push_back(i);
  }
  return indices;
}

std::vector<size_t> QuickPickActivity::relevantHabitIndices() const {
  std::vector<size_t> indices;
  const auto& habits = HABITIFY_HABITS.getHabits();
  indices.reserve(habits.size());
  for (size_t i = 0; i < habits.size(); i++) {
    if (!habits[i].isComplete()) indices.push_back(i);
  }
  return indices;
}

std::vector<std::string> QuickPickActivity::logEntries() const {
  const auto& titles = TODOIST_TASKS.getCompletedTodayTitles();
  std::vector<std::string> entries;
  entries.reserve(titles.size() + HABITIFY_HABITS.getHabits().size());
  for (const auto& title : titles) entries.push_back(title);
  for (const auto& habit : HABITIFY_HABITS.getHabits()) {
    if (habit.isComplete()) entries.push_back(habit.name);
  }
  return entries;
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

void QuickPickActivity::offerClearLogs() {
  if (logEntries().empty()) return;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_LOG_CLEAR_CONFIRM), ""),
                         [this](const ActivityResult& result) {
                           // Same reasoning as every other popup this screen pushes (see
                           // swallowConfirmRelease/swallowBackRelease's own comment): a button
                           // still held when the popup resolves would otherwise fire a stale
                           // release here the moment it is actually released.
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = true;
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back))
                             swallowBackRelease = true;
                           if (result.isCancelled) return;
                           TODOIST_TASKS.clearCompletedNow();
                           TODOIST_TASKS.saveToFile();
                           HABITIFY_HABITS.clearCompletedNow();
                           HABITIFY_HABITS.saveToFile();
                           // Same reasoning as LogsActivity's own equivalent call: every other
                           // mutator of today's counts recalculates the mood ladder afterwards,
                           // so clearing them without doing the same here would leave the
                           // companion showing whatever mood the now-cleared counts had earned.
                           COMPANION.recordActivity();
                           requestUpdate(true);
                         });
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

void QuickPickActivity::afterRowAction() {
  if (!currentPickStillEligible()) reroll();
  requestUpdate(true);
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
                           afterRowAction();
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
                           afterRowAction();
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
  afterRowAction();
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
                           afterRowAction();
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
  afterRowAction();
}

// -- Tasks tab row actions ----------------------------------------------------

void QuickPickActivity::showTaskRowOptions(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;

  // Same reasoning as showOptions(): Todoist cannot reschedule a single
  // occurrence of a recurring task, so Reschedule is left off for one.
  const bool canReschedule = !tasks[cacheIndex].isRecurring;

  std::vector<std::string> options{tr(STR_COMPLETE_TASK), tr(STR_FOCUS_SESSION)};
  if (canReschedule) options.push_back(tr(STR_RESCHEDULE_TASK));
  const int rescheduleIdx = canReschedule ? 2 : -1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, cacheIndex, rescheduleIdx](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          completeTaskRow(cacheIndex);
        } else if (idx == 1) {
          offerFocusSessionForTask(cacheIndex);
        } else if (idx == rescheduleIdx) {
          offerRescheduleRow(cacheIndex);
        }
      });
}

void QuickPickActivity::offerFocusSessionForTask(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;
  const std::string text = tasks[cacheIndex].content;
  const std::string id = tasks[cacheIndex].id;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, text, id](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(text, id, /*isHabit=*/false,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
                         });
}

void QuickPickActivity::offerRescheduleRow(const size_t cacheIndex) {
  std::vector<std::string> options;
  options.push_back(tr(STR_PICK_DATE));
  options.push_back(tr(STR_TASKS_TAB_NO_DATE));

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_RESCHEDULE_TASK, std::move(options)),
      [this, cacheIndex](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          offerRescheduleDatePickerRow(cacheIndex);
        } else if (idx == 1) {
          clearTaskDueDateRow(cacheIndex);
        }
      });
}

void QuickPickActivity::offerRescheduleDatePickerRow(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;
  const uint16_t seed = tasks[cacheIndex].dueDays != todoist::DUE_NONE
                            ? tasks[cacheIndex].dueDays
                            : todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  startActivityForResult(std::make_unique<RescheduleTaskActivity>(renderer, mappedInput, seed),
                         [this, cacheIndex](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const auto* date = std::get_if<DateResult>(&result.data);
                           if (!date) return;
                           if (cacheIndex >= TODOIST_TASKS.getTasks().size()) return;

                           {
                             RenderLock lock(*this);
                             organizerActions::rescheduleTask(cacheIndex, date->packedDate);
                           }
                           afterRowAction();
                         });
}

void QuickPickActivity::clearTaskDueDateRow(const size_t cacheIndex) {
  if (cacheIndex >= TODOIST_TASKS.getTasks().size()) return;
  {
    RenderLock lock(*this);
    organizerActions::rescheduleTask(cacheIndex, todoist::DUE_NONE);
  }
  afterRowAction();
}

void QuickPickActivity::completeTaskRow(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;

  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                                                tasks[cacheIndex].content),
                         [this, cacheIndex](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           if (cacheIndex < TODOIST_TASKS.getTasks().size()) {
                             RenderLock lock(*this);
                             organizerActions::completeTask(cacheIndex);
                           }
                           afterRowAction();
                         });
}

// -- Habits tab row actions ---------------------------------------------------

void QuickPickActivity::showHabitRowOptions(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return;
  // Same reasoning as showOptions(): a goal-less habit has no unit, so Log
  // is left off the menu rather than shown and silently failing.
  const bool canLog = !habits[cacheIndex].unitSymbol.empty();

  std::vector<std::string> options;
  if (canLog) options.push_back(tr(STR_HABITIFY_LOG));
  options.push_back(tr(STR_COMPLETE_HABIT));
  options.push_back(tr(STR_FOCUS_SESSION));
  const int logIdx = canLog ? 0 : -1;
  const int completeIdx = canLog ? 1 : 0;
  const int focusIdx = canLog ? 2 : 1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, cacheIndex, logIdx, completeIdx, focusIdx](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == logIdx) {
          logHabitRow(cacheIndex);
        } else if (idx == completeIdx) {
          completeHabitRow(cacheIndex);
        } else if (idx == focusIdx) {
          offerFocusSessionForHabit(cacheIndex);
        }
      });
}

void QuickPickActivity::offerFocusSessionForHabit(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return;
  const std::string text = habits[cacheIndex].name;
  const std::string id = habits[cacheIndex].id;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, text, id](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(text, id, /*isHabit=*/true,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
                         });
}

void QuickPickActivity::logHabitRow(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return;
  if (habits[cacheIndex].unitSymbol.empty()) return;
  const auto& habit = habits[cacheIndex];

  startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                             renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                             MAX_HABIT_LOG_AMOUNT, HABIT_LOG_SMALL_STEP, HABIT_LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                             /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true, StrId::STR_NONE_OPT,
                             habit.name, habit.unitSymbol),
                         [this, cacheIndex](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const auto amount = std::get<IntervalResult>(result.data).value;
                           if (cacheIndex < HABITIFY_HABITS.getHabits().size()) {
                             RenderLock lock(*this);
                             organizerActions::logHabit(cacheIndex, static_cast<float>(amount));
                           }
                           afterRowAction();
                         });
}

void QuickPickActivity::completeHabitRow(const size_t cacheIndex) {
  if (cacheIndex >= HABITIFY_HABITS.getHabits().size()) return;
  {
    RenderLock lock(*this);
    organizerActions::completeHabit(cacheIndex);
  }
  afterRowAction();
}

// -- input --------------------------------------------------------------------

void QuickPickActivity::loop() {
  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) swallowBackRelease = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = false;

  // Side Up/Down switch tabs, from wherever the cursor already is -- same
  // convention every other app screen follows. A fresh press each, same
  // guard reasoning as Back/Confirm.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) upPressSeen = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) downPressSeen = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upPressSeen) {
      switchTab(previousTab());
      requestUpdate(true);
    }
    upPressSeen = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downPressSeen) {
      switchTab(nextTab());
      requestUpdate(true);
    }
    downPressSeen = false;
    return;
  }

  // Back always leaves, in every tab (see this file's own header comment).
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

  // Confirm/Left/Right: whatever the active tab needs (see this file's own
  // header comment for the full scheme per tab).
  if (activeTab == Tab::Logs) {
    if (!poolEmpty && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      reroll();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      offerClearLogs();
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
    return;
  }

  if (activeTab == Tab::Tasks) {
    const auto indices = relevantTaskIndices();
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!indices.empty()) {
        taskSelectedRow = static_cast<int>((taskSelectedRow + indices.size() - 1) % indices.size());
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (!indices.empty()) {
        taskSelectedRow = static_cast<int>((static_cast<size_t>(taskSelectedRow) + 1) % indices.size());
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (swallowConfirmRelease) {
        swallowConfirmRelease = false;
        return;
      }
      if (!indices.empty() && taskSelectedRow >= 0 && static_cast<size_t>(taskSelectedRow) < indices.size()) {
        showTaskRowOptions(indices[static_cast<size_t>(taskSelectedRow)]);
      }
    }
    return;
  }

  // activeTab == Tab::Habits
  const auto indices = relevantHabitIndices();
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (!indices.empty()) {
      habitSelectedRow = static_cast<int>((habitSelectedRow + indices.size() - 1) % indices.size());
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (!indices.empty()) {
      habitSelectedRow = static_cast<int>((static_cast<size_t>(habitSelectedRow) + 1) % indices.size());
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    if (!indices.empty() && habitSelectedRow >= 0 && static_cast<size_t>(habitSelectedRow) < indices.size()) {
      showHabitRowOptions(indices[static_cast<size_t>(habitSelectedRow)]);
    }
  }
}

// -- render ---------------------------------------------------------------

void QuickPickActivity::renderLogsTab(const int top, const int height) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int centreX = pageWidth / 2;
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;

  // Mood label, centred under the companion figure -- same treatment and
  // same SETTINGS.companionShowMoodLabel gating as Home's own drawCompanion(),
  // just without its bulleted today's-tasks/habits-completed lines (today's
  // completions are the log list right below instead).
  int listTop = top;
  if (SETTINGS.companionShowMoodLabel != 0) {
    const char* label = companion::moodLabel(COMPANION.currentMood());
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, centreX - labelW / 2, top, label, true, EpdFontFamily::BOLD);
    listTop = top + renderer.getLineHeight(UI_10_FONT_ID) + ROW_GAP;
  }
  const int listHeight = std::max(0, top + height - listTop);

  // Today's completed tasks/habits -- same source and same "cleared by the
  // Left button" convention LogsActivity had as its own dedicated screen.
  const auto entries = logEntries();
  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listHeight / 2, tr(STR_LOG_EMPTY));
    return;
  }

  const int pageItems = std::max(1, listHeight / ROW_HEIGHT);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  for (int row = 0; row < pageItems; row++) {
    if (row >= static_cast<int>(entries.size())) break;
    const int rowY = listTop + row * ROW_HEIGHT;
    const auto shown = renderer.truncatedText(UI_10_FONT_ID, entries[static_cast<size_t>(row)].c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, rowY + (ROW_HEIGHT - lineH) / 2, shown.c_str(), true);

    // Nothing here is selectable, so (unlike the Tasks/Habits tabs) every row
    // gets this dithered separator rather than just the ones a selection
    // fill does not already bound.
    const bool lastOnPage = row + 1 >= pageItems || row + 1 >= static_cast<int>(entries.size());
    if (!lastOnPage) {
      renderer.fillRectDither(textX, rowY + ROW_HEIGHT - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                              Color::LightGray);
    }
  }
}

void QuickPickActivity::renderTasksTab(const int top, const int height) const {
  const auto indices = relevantTaskIndices();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;

  if (indices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, tr(STR_TODOIST_NO_TASKS));
    return;
  }

  const int pageItems = std::max(1, height / ROW_HEIGHT);
  const int pageStart = (taskSelectedRow / pageItems) * pageItems;
  const auto& tasks = TODOIST_TASKS.getTasks();

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(indices.size())) break;
    const size_t cacheIndex = indices[static_cast<size_t>(i)];
    if (cacheIndex >= tasks.size()) continue;
    const int rowY = top + row * ROW_HEIGHT;
    const bool selected = i == taskSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);

    // Overdue leads as a small bold tag, so the one thing this filtered list
    // cannot otherwise show (why a task is here) is not lost by trimming the
    // real screen's own due-date subtitle down to one line.
    const char* tag = tasks[cacheIndex].overdue ? tr(STR_OVERDUE) : nullptr;
    int rowX = textX;
    int rowMaxWidth = textWidth;
    if (tag != nullptr) {
      const int tagW = renderer.getTextWidth(UI_10_FONT_ID, tag, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rowX, rowY + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2, tag, ink,
                        EpdFontFamily::BOLD);
      rowX += tagW + metrics.contentSidePadding / 2;
      rowMaxWidth -= tagW + metrics.contentSidePadding / 2;
    }
    const auto shown = renderer.truncatedText(UI_10_FONT_ID, tasks[cacheIndex].content.c_str(), rowMaxWidth);
    renderer.drawText(UI_10_FONT_ID, rowX, rowY + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      shown.c_str(), ink);
  }
}

void QuickPickActivity::renderHabitsTab(const int top, const int height) const {
  const auto indices = relevantHabitIndices();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;

  if (indices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, tr(STR_HABITIFY_ALL_DONE));
    return;
  }

  const int pageItems = std::max(1, height / ROW_HEIGHT);
  const int pageStart = (habitSelectedRow / pageItems) * pageItems;
  const auto& habits = HABITIFY_HABITS.getHabits();

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(indices.size())) break;
    const size_t cacheIndex = indices[static_cast<size_t>(i)];
    if (cacheIndex >= habits.size()) continue;
    const auto& habit = habits[cacheIndex];
    const int rowY = top + row * ROW_HEIGHT;
    const bool selected = i == habitSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);

    char progress[24];
    // %g rather than %f: a count-based habit reads "1/3", not "1.000000/3.000000".
    if (habit.hasTarget()) {
      snprintf(progress, sizeof(progress), "%g/%g", static_cast<double>(habit.shownCurrent()),
               static_cast<double>(habit.target));
    } else {
      snprintf(progress, sizeof(progress), "%g", static_cast<double>(habit.shownCurrent()));
    }

    const int gap = renderer.getSpaceWidth(UI_10_FONT_ID) * 2;
    const int progressWidth = renderer.getTextWidth(UI_10_FONT_ID, progress);
    const int nameWidth = std::max(0, textWidth - progressWidth - gap);
    const auto shownName = renderer.truncatedText(UI_10_FONT_ID, habit.name.c_str(), nameWidth);
    const int textY = rowY + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, shownName.c_str(), ink);
    renderer.drawText(UI_10_FONT_ID, textX + textWidth - progressWidth, textY, progress, ink);
  }
}

void QuickPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Age and highscore sit in the header's own status column, the same spot
  // Tasks/Calendar/Budget/Habits show their sync date -- lifetime info that,
  // unlike the Logs tab's own body, never changes tab to tab.
  char status[64];
  const std::string ageValue = CompanionTracker::formatAge(COMPANION_STATE.activatedDay);
  snprintf(status, sizeof(status), "%s: %s  \xC2\xB7  %s: %u", tr(STR_COMPANION_AGE), ageValue.c_str(),
           tr(STR_COMPANION_HIGHSCORE), COMPANION_STATE.ledger.bestDayPoints);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 CompanionTracker::displayName(), status);

  const std::vector<TabInfo> tabs = {
      {tr(STR_COMPANION_TAB_TASKS), activeTab == Tab::Tasks},
      {tr(STR_COMPANION_TAB_HABITS), activeTab == Tab::Habits},
      {tr(STR_COMPANION_TAB_LOGS), activeTab == Tab::Logs},
  };
  // Always drawn as "focused": there is no separate level where the cursor
  // sits on the tab bar itself here (side Up/Down switch it directly), so
  // the active tab's own emphasis is never competing with anything else.
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 true);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int totalContentHeight = contentBottom - contentTop;
  const int contentWidth = pageWidth - MARGIN * 2;
  const int maxTextWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();

  const std::string text = mood == companion::Mood::Sleeping ? std::string(tr(STR_COMPANION_SLEEPING_BUBBLE))
                           : poolEmpty                       ? std::string(tr(STR_QUICK_PICK_EMPTY))
                                                             : pickedText;
  const auto textFit = companion::fitBubbleText(renderer, UI_10_FONT_ID, text, maxTextWidth, MIN_BUBBLE_TEXT_WIDTH, 4);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(textFit.lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int bubbleWidth = textFit.textWidth + PAD * 2;

  // The companion figure (bubble + sprite) gets a fixed share of the space
  // below the tab bar, present unchanged in every tab -- see this file's own
  // header comment. The active tab's own content gets what is left over.
  const int companionBudget = totalContentHeight * COMPANION_BUDGET_PERCENT / 100;
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) > contentWidth) continue;
    if (bubbleBlock + companion::poseHeight(candidate) <= companionBudget) {
      scale = candidate;
      break;
    }
  }

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int centreX = pageWidth / 2;
  const int bubbleX = centreX - bubbleWidth / 2;

  companion::drawSpeechBubble(renderer, bubbleX, contentTop, bubbleWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom);
  const Rect textBounds{bubbleX + PAD, contentTop, textFit.textWidth, bubbleH};
  int textY = contentTop + PAD;
  for (const auto& line : textFit.lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
    textY += lineH;
  }

  const int spriteTop = contentTop + bubbleBlock;
  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, spriteTop, scale);

  const int tabContentTop = spriteTop + spriteH + LABEL_GAP;
  const int tabContentHeight = std::max(0, contentBottom - tabContentTop);

  switch (activeTab) {
    case Tab::Logs:
      renderLogsTab(tabContentTop, tabContentHeight);
      break;
    case Tab::Tasks:
      renderTasksTab(tabContentTop, tabContentHeight);
      break;
    case Tab::Habits:
      renderHabitsTab(tabContentTop, tabContentHeight);
      break;
  }

  // Back always lands on Home in every launch path this screen has (see its
  // own header comment: "Back returns to Home") -- whether that is really
  // finish() popping back to the HomeActivity caller (activateCompanion())
  // or a replaceActivity() hand-off with no caller pushed at all
  // (FocusSessionActivity, the boot quick-resume path), never a return to
  // some other, non-Home screen. So this says Home, not Back, in every tab.
  const char* backLabel = tr(STR_HOME);
  const char* confirmLabel = "";
  const char* leftLabel = "";
  const char* rightLabel = "";
  if (activeTab == Tab::Logs) {
    confirmLabel = poolEmpty ? "" : tr(STR_SELECT);
    leftLabel = logEntries().empty() ? "" : tr(STR_CLEAR_BUTTON);
    rightLabel = poolEmpty ? "" : tr(STR_QUICK_PICK_RANDOM);
  } else {
    const bool hasRows = activeTab == Tab::Tasks ? !relevantTaskIndices().empty() : !relevantHabitIndices().empty();
    confirmLabel = hasRows ? tr(STR_SELECT) : "";
    leftLabel = tr(STR_DIR_UP);
    rightLabel = tr(STR_DIR_DOWN);
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
