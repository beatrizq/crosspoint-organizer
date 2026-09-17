#pragma once

#include <CompanionMood.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * The companion's own screen: a big pose of its figure, mood and speech
 * bubble (a task/habit suggestion, or the sleeping/empty text -- never a BLE
 * notification; those stay on the dedicated Alerts screen), with today's Logs
 * (completed tasks/habits) always shown underneath. No tabs, no Tasks/Habits
 * browsing here -- those live in their own real screens; the mood credit for
 * completing one still updates instantly because acting on it happens
 * through this screen's own single suggestion (Random/Select), not a row
 * list. Age and highscore (lifetime info) sit in the header's own status
 * column; the companion's name is the header title itself (see
 * CompanionTracker::displayName()).
 *
 * Reached from Home (the companion's own grid tile) or reconstructed on boot
 * from CrossPointState when the device was showing this screen at the moment
 * it went to sleep -- either way, everything this screen needs comes through
 * the constructor, since it also mirrors its own content into CrossPointState
 * on entry and on every reroll (see onEnter()/reroll()) rather than main.cpp
 * fishing it out reactively.
 *
 * Button IDs used here: Left1/Left2 are the pair that moves focus up/down
 * through the Logs row list (Left1 = up, Left2 = down); Right1/Right2 are the
 * pair printed "Apps"/"Select" on the case (Right1 = the button that
 * otherwise always leaves, Right2 = the button that otherwise always opens
 * options). The Logs row list and the companion figure form one continuous
 * circular loop: Left1 off row 0, or Left2 off the last row, moves focus onto
 * the companion figure itself (a rounded selection-box outline spanning
 * (almost) the full content width, bounding the companion section alone --
 * mood label, bubble and sprite -- never the Logs section below it: this
 * screen reads as two sections, companion and Logs, and the highlight only
 * ever claims the one that's focused. Same rounded "outline, not fill" style
 * the pre-grid-tile Home screen once drew around its own companion column)
 * rather than wrapping straight to the opposite end of the list. From there
 * Right2 stays Select (acts on the suggestion) and Right1 -- which otherwise
 * always leaves -- becomes Random instead (on different buttons than a row's
 * own Left1/Left2, which are busy continuing the loop: Left1 on to the last
 * row, Left2 back to the first). Right1 leaves in every other state,
 * reporting the current suggestion as a QuickPickResult so Home's own
 * companion tile stays in sync. setResult() has to be called before finish(),
 * not in onExit() -- ActivityManager::popActivity() reads the result before
 * it runs the outgoing activity's onExit().
 *
 * The Logs row list itself is Select-less -- Right2 on a row is Clear
 * instead, and only when that row is Cached (see LogEntry::cached and
 * clearLogRow()'s own comment): a completion still local/unpushed, actually
 * reversible, as opposed to Synced (a sync already confirmed it, nothing
 * local left to undo, Right2 does nothing there).
 *
 * Side Up/Down jump to the previous/next app in the home grid's own order --
 * the same shortcut every app screen has (see
 * OrganizerScreenActivity/SettingsActivity's own identical block),
 * independent of Left1/Left2/Right1/Right2 above.
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
  // Rerolls via quickpick::roll() -- the same pool/weights Home's own roll
  // used -- and re-mirrors the result into CrossPointState.
  void reroll();

  // One row of the Logs row list -- see logEntries()'s own comment.
  struct LogEntry {
    std::string text;
    bool isTask;  // true: taskEntryIndex is valid. false: habitId is valid.
    // Whether Clear (Right2, see loop()'s own comment) is offered for this
    // row: Cached (still local/unpushed -- safely, actually reversible) vs
    // Synced (a sync already confirmed it -- nothing local left to undo).
    bool cached;
    size_t taskEntryIndex;  // Index into TODOIST_TASKS.getCompletedTodayEntries().
    std::string habitId;    // HABITIFY_HABITS habit id.
  };
  // Today's completed tasks/habits, in completion order -- same source
  // LogsActivity's own loadEntries() once read (TodoistTaskCache::
  // getCompletedTodayEntries() plus completed habits). Rebuilt fresh every
  // call rather than stored.
  std::vector<LogEntry> logEntries() const;
  // Right2 on a Cached row (see clearLogRow()'s own comment for what "Cached"
  // means for a task vs. a habit): actually reverses the completion --
  // TodoistTaskCache::cancelCompletedLogEntry() for a task,
  // HabitifyHabitCache::undoLocalCompletion() for a habit. A Synced row (a
  // sync already confirmed it) has nothing local left to undo, so Right2 is
  // simply not offered there (see render()'s own confirmLabel logic) --
  // clearLogRow() is never called for one.
  void clearLogRow(const LogEntry& entry);

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

  // Common tail of every suggestion action above: rerolls the suggestion if
  // what it was showing is no longer eligible, then repaints.
  void afterRowAction();

  // Draws the Logs row list below the companion figure/bubble.
  void renderLogsTab(int top, int height) const;

  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;

  // Row cursor into logEntries(), not a cache index.
  int logSelectedRow = 0;
  // Left1 off row 0, or Left2 off the last row, moves focus here instead of
  // wrapping to the opposite end of the list -- one stop above the row list,
  // not a third index space of its own, so no separate cursor position is
  // needed. Left1/Left2 continue the same circular traversal back into the
  // row list (last/first row respectively) while this is true.
  bool companionFocused = false;

  // Mirrors HomeActivity's own lastCompanionRefreshMs/lastCompanionMood --
  // this screen is reachable directly from sleep (CrossPointState
  // reconstruction) and can sit open just as long as Home, so it needs the
  // same idle re-check rather than only refreshing once at onEnter() (see
  // loop()'s own comment): without it, a mood that should have decayed, or
  // crossed into the sleep window, while this screen was already open
  // stayed stale until the user happened to visit Home, which is the only
  // other place that calls CompanionTracker::refreshForDisplay().
  unsigned long lastCompanionRefreshMs = 0;
  companion::Mood lastCompanionMood = companion::Mood::Happy;

  // See OrganizerScreenActivity's own swallow flags for why these exist: the
  // Options popup (and the confirmation or number entry it can lead to)
  // answers on a button press, not its release, and that release is still
  // owed to this screen once the sub-activity it was pushed from closes.
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;
  // Side Up/Down jump to the previous/next app in the home grid's own order
  // -- the same shortcut every other app screen has (see
  // OrganizerScreenActivity/SettingsActivity's own identical block). Guarded
  // by a fresh-press check the same way Right1/Right2 are above, in case one
  // was already held down when some other gesture left this screen.
  bool upPressSeen = false;
  bool downPressSeen = false;
};
