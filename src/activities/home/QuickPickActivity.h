#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * The companion's own screen: its figure, mood and speech bubble stay on
 * screen across three tabs -- Logs (today's log of completed tasks/habits,
 * plus the same suggestion Random/Go act on), Tasks and Habits (the same
 * items that count toward the mood: due-today-or-overdue tasks, not-yet-done
 * habits) -- so acting on one of them updates the mood right where it is
 * shown, without leaving to the real Tasks/Habits screens. Age and highscore
 * (lifetime info, not today's) sit in the header's own status column instead
 * -- the same spot Tasks/Calendar/Budget/Habits show their sync date -- since
 * neither changes tab to tab; the companion's name is the header title
 * itself (see CompanionTracker::displayName()), so there is nothing left to
 * repeat in the Logs tab's own body.
 *
 * Reached from Home (companion focused, then activated) or reconstructed on
 * boot from CrossPointState when the device was showing this screen at the
 * moment it went to sleep -- either way, everything the Logs tab needs comes
 * through the constructor, since it also mirrors its own content into
 * CrossPointState on entry and on every reroll (see onEnter()/reroll())
 * rather than main.cpp fishing it out reactively.
 *
 * Side Up/Down switch tabs (see every other app screen's own convention);
 * front buttons are whatever the active tab needs: Logs keeps Confirm/Right
 * as Select/Random exactly as before there was more than one tab (Confirm
 * opens the same [action, Focus session] options showOptions() always has),
 * and Left is Clear -- LogsActivity's own former Confirm action, folded in
 * now that its entries live in this tab's own body instead of a separate
 * screen. Tasks and Habits use Left/Confirm/Right as Up/Select/Down over
 * that tab's own row list, the same shape a real row list's front buttons
 * already have elsewhere. Back always leaves, in every tab, reporting
 * whatever the Logs tab currently holds as a QuickPickResult so Home's own
 * bubble stays in sync. setResult() has to be called before finish(), not in
 * onExit() -- ActivityManager::popActivity() reads the result before it runs
 * the outgoing activity's onExit().
 */
class QuickPickActivity final : public Activity {
 public:
  // itemId is the Todoist task id / Habitify habit id behind pickedText, so
  // Go can act on that exact item. Empty when poolEmpty is true (nothing was
  // picked).
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pickedText, std::string itemId,
                    const bool isHabit, const bool poolEmpty)
      : Activity("QuickPick", renderer, mappedInput),
        pickedText(std::move(pickedText)),
        itemId(std::move(itemId)),
        isHabit(isHabit),
        poolEmpty(poolEmpty) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isQuickPickActivity() const override { return true; }

 private:
  enum class Tab : uint8_t { Tasks = 0, Habits = 1, Logs = 2 };
  static constexpr int TAB_COUNT = 3;

  Tab nextTab() const { return static_cast<Tab>((static_cast<int>(activeTab) + 1) % TAB_COUNT); }
  Tab previousTab() const { return static_cast<Tab>((static_cast<int>(activeTab) + TAB_COUNT - 1) % TAB_COUNT); }
  // Resets the row cursor of whichever tab is switched to -- the old cursor
  // was into a different list, and rebuilding that list to check it is still
  // in range is not worth it for what a fresh 0 already gives for free.
  void switchTab(Tab next);

  // The cache indices that count toward the companion's mood right now --
  // same criteria quickpick::roll() itself pools from (see its own
  // comment): tasks overdue or due today, habits not yet complete. Rebuilt
  // fresh every time rather than stored, the same way roll()'s own pool is -
  // cheap, and never goes stale across a completion or a sync.
  std::vector<size_t> relevantTaskIndices() const;
  std::vector<size_t> relevantHabitIndices() const;

  // Rerolls via quickpick::roll() -- the same pool/weights Home's own roll
  // used -- and re-mirrors the result into CrossPointState.
  void reroll();

  // Today's completed tasks/habits, in completion order -- same source
  // LogsActivity's own loadEntries() read (TodoistTaskCache::
  // getCompletedTodayTitles() plus completed habits), just titles only:
  // this tab's own row list has no subtitle to put a source app in, the way
  // LogsActivity's did.
  std::vector<std::string> logEntries() const;
  // Left, Logs tab only -- LogsActivity's own former Confirm action, moved
  // here now that its list lives in this tab's own body. Same confirm-then-
  // clear-both-caches behaviour, and the same mood recalculation afterwards
  // (see its own comment for why that call is needed at all).
  void offerClearLogs();

  // Go opens this. Same [action, Focus session] choice Tasks/Habits show on
  // a row, resolved against itemId rather than a selected row.
  void showOptions();
  void completeSuggestedTask();
  void logSuggestedHabit();
  // The Options menu's "Complete" entry for a habit suggestion opens this:
  // marks it done directly, via organizerActions::completeHabit() - works
  // even for a goal-less habit logSuggestedHabit()'s number entry cannot
  // touch.
  void completeSuggestedHabit();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the suggested item.
  void offerFocusSession();
  // The Options menu's "Reschedule" entry opens this (task suggestions only -
  // a habit has no due date): a sub-choice between picking a new date and
  // clearing the due date entirely.
  void offerReschedule();
  // "Pick a date" from offerReschedule()'s sub-menu: the date picker itself.
  void offerRescheduleDatePicker();
  // "No date" from offerReschedule()'s sub-menu: clears the due date directly,
  // no further confirmation - same immediacy as Complete.
  void clearTaskDueDate();

  // Whether the current pick is still a valid quickpick candidate: present in
  // its cache and, for a habit, still short of its target. Checked once an
  // action has actually mutated the cache -- a completed task is gone from
  // the cache outright, and a habit logged to its target drops out the same
  // way roll()'s own pool would exclude it. Only then is a fresh suggestion
  // rolled; Focus session, a cancelled popup, or a habit log that leaves it
  // still short of target all leave the bubble showing exactly what it did
  // before.
  bool currentPickStillEligible() const;

  // Tasks tab row action -- mirrors TasksActivity's own showRowOptions() and
  // the actions it leads to, resolved against a cache index straight from
  // relevantTaskIndices() rather than a row list this screen owns a copy of.
  void showTaskRowOptions(size_t cacheIndex);
  void completeTaskRow(size_t cacheIndex);
  void offerRescheduleRow(size_t cacheIndex);
  void offerRescheduleDatePickerRow(size_t cacheIndex);
  void clearTaskDueDateRow(size_t cacheIndex);
  void offerFocusSessionForTask(size_t cacheIndex);

  // Habits tab row action -- mirrors HabitsActivity's own showRowOptions().
  void showHabitRowOptions(size_t cacheIndex);
  void logHabitRow(size_t cacheIndex);
  void completeHabitRow(size_t cacheIndex);
  void offerFocusSessionForHabit(size_t cacheIndex);

  // Common tail of every row action above: rerolls the Logs tab's own
  // suggestion if what it was showing is no longer eligible, then repaints.
  void afterRowAction();

  // render()'s own three tab bodies, sharing the rect below the companion
  // figure and speech bubble (present, unchanged, in all three -- see this
  // file's own header comment).
  void renderLogsTab(int top, int height) const;
  void renderTasksTab(int top, int height) const;
  void renderHabitsTab(int top, int height) const;

  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;

  Tab activeTab = Tab::Tasks;
  // Row cursor within Tasks'/Habits' own filtered list -- an index into
  // relevantTaskIndices()/relevantHabitIndices(), not a cache index itself.
  int taskSelectedRow = 0;
  int habitSelectedRow = 0;

  // See OrganizerScreenActivity's own swallow flags for why these exist: the
  // Options popup (and the confirmation or number entry it can lead to)
  // answers on a button press, not its release, and that release is still
  // owed to this screen once the sub-activity it was pushed from closes.
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;
  // Side Up/Down switch tabs -- guarded by a fresh-press check the same way
  // every other app screen's own upPressSeen/downPressSeen are, in case one
  // was already held down when some other gesture landed on this screen.
  bool upPressSeen = false;
  bool downPressSeen = false;
};
