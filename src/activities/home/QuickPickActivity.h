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
 * front buttons are whatever the active tab needs. Button IDs used here:
 * Left1/Left2 are the pair that moves focus up/down through a row list
 * (Left1 = up, Left2 = down); Right1/Right2 are the pair printed "Apps"/
 * "Select" on the case (Right1 = the button that otherwise always leaves,
 * Right2 = the button that otherwise always opens options).
 *
 * All three tabs use Left1/Right2/Left2 as Up/Select/Down over that tab's own
 * row list, the same shape a real row list's front buttons already have
 * elsewhere -- and in every tab, the row list and the companion figure form
 * one continuous circular loop: Left1 off row 0, or Left2 off the last row,
 * moves focus onto the companion figure itself (dithered light-grey
 * highlight behind it, the same "selected" treatment ReadMenuActivity's own
 * recent-book cover uses) rather than wrapping straight to the opposite end
 * of the list, since the figure always represents the same suggestion
 * regardless of which tab is showing below it. From there Right2 stays
 * Select, and Right1 -- which otherwise always leaves -- becomes Random
 * instead (on different buttons than a row's own Left1/Left2, which are busy
 * continuing the loop: Left1 on to the last row, Left2 back to the first).
 * Right1 leaves in every other state, reporting whatever the Logs tab
 * currently holds as a QuickPickResult so Home's own bubble stays in sync.
 * setResult() has to be called before finish(), not in onExit() --
 * ActivityManager::popActivity() reads the result before it runs the
 * outgoing activity's onExit().
 *
 * Logs' own row list (today's completed tasks/habits) is Select-less --
 * Right2 on a row is Clear instead, and only when that row is Cached (see
 * LogEntry::cached and clearLogRow()'s own comment): a completion still
 * local/unpushed, actually reversible, as opposed to Synced (a sync already
 * confirmed it, nothing local left to undo, Right2 does nothing there).
 *
 * A pending BLE notification (see BleNotificationQueue, ENABLE_BLE_NOTIFY_SPIKE
 * builds only) takes over the bubble ahead of the sleeping/empty/suggestion
 * text above, in every tab -- newest first, with a "+N more" suffix when
 * others are queued behind it (see hasPendingNotification()/
 * notificationBubbleText()). While the companion is focused (any tab),
 * Right1/Right2 become Dismiss/Dismiss All instead of Random/Select for as
 * long as one is showing; neither ever touches the real queue, so a
 * dismissed notification still shows in the real Alerts screen until cleared
 * there.
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
  enum class Tab : uint8_t { Tasks = 0, Habits = 1, Calendar = 2, Budget = 3, Logs = 4 };
  static constexpr int TAB_COUNT = 5;

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

  // One row of the Logs tab's own row list -- see logEntries()'s own comment.
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
  // LogsActivity's own loadEntries() read (TodoistTaskCache::
  // getCompletedTodayEntries() plus completed habits). Rebuilt fresh every
  // call, the same as relevantTaskIndices()/relevantHabitIndices() are.
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

  // render()'s own five tab bodies, sharing the rect below the companion
  // figure and speech bubble (present, unchanged, in all five -- see this
  // file's own header comment).
  void renderLogsTab(int top, int height) const;
  void renderTasksTab(int top, int height) const;
  void renderHabitsTab(int top, int height) const;
  // Calendar/Budget mirror CalendarActivity's own "All" tab and
  // BudgetActivity's own "Plan" tab -- both read-only there (see their own
  // class doc comments: nothing is ever pushed back for either), so unlike
  // Tasks/Habits these have no row action at all, the same as Logs' own rows
  // (aside from Logs' Clear, which is Logs-specific). Condensed to one line
  // per row (title + a compact right-aligned "when"/balance), matching this
  // screen's own "glanceable subset, not a replica" convention -- neither
  // real tab's own richer row (Calendar's two-line title+when, or an inflow
  // category's bold styling) is reproduced here.
  void renderCalendarTab(int top, int height) const;
  void renderBudgetTab(int top, int height) const;

  // Re-syncs notificationsDismissed against BLE_NOTIFICATIONS' current state
  // -- resets to 0 if the newest entry changed since last checked (a new
  // alert always takes the bubble back over), and clamps against getCount()
  // in case the real Alerts screen's own dismiss-all shrank the queue out
  // from under this screen. Called every loop() regardless of active tab, so
  // an arrival while idle here is never more than one frame stale. A no-op
  // when ENABLE_BLE_NOTIFY_SPIKE isn't compiled in -- declared unconditionally
  // so the call site in loop() never needs its own #ifdef.
  void refreshNotificationState();
  // Whether a BLE notification currently occupies the companion bubble
  // instead of pickedText. Always false when ENABLE_BLE_NOTIFY_SPIKE isn't
  // compiled in.
  bool hasPendingNotification() const;
  // The bubble's own "Title: content (+N more)" text for the current
  // notification -- only meaningful when hasPendingNotification() is true.
  std::string notificationBubbleText() const;
  // Right1 while focused, when a notification is showing: reveals the next-
  // newest queued one. Never touches BLE_NOTIFICATIONS itself, so a
  // dismissed notification still shows in the real Alerts screen until
  // cleared there separately.
  void dismissTopNotification();
  // Right2 while focused, when a notification is showing: hides the whole
  // queued stack from this bubble (same non-destructive semantics as above).
  void dismissAllNotifications();

  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;

  Tab activeTab = Tab::Tasks;
  // Row cursor within each tab's own list -- an index into
  // relevantTaskIndices()/relevantHabitIndices()/logEntries(), or straight
  // into GCAL_EVENTS.getEvents()/YNAB_CATEGORIES.getCategories() for
  // Calendar/Budget (unfiltered, unlike Tasks/Habits -- see
  // renderCalendarTab()/renderBudgetTab()'s own comment), not a cache index.
  int taskSelectedRow = 0;
  int habitSelectedRow = 0;
  int logSelectedRow = 0;
  int calendarSelectedRow = 0;
  int budgetSelectedRow = 0;
  // Left1 off row 0, or Left2 off the last row, moves focus here instead of
  // wrapping to the opposite end of the list -- one stop above the row list,
  // not a third index space of its own, so no separate cursor position is
  // needed. Left1/Left2 continue the same circular traversal back into the
  // row list (last/first row respectively) while this is true. switchTab()
  // resets it on every tab change.
  bool companionFocused = false;

  // Tracks how many of BLE_NOTIFICATIONS' newest entries are currently
  // hidden from this bubble via Dismiss/Dismiss All (see
  // refreshNotificationState()'s own comment) -- never mutates the real
  // queue. hasNotificationSnapshot/lastNotification* are a cheap composite
  // key (id alone isn't reliable: calls always use id=0, and a BLE reconnect
  // can resend an existing id -- see BleNotificationEntry's own field
  // comment) used to detect "the newest entry changed" without storing a
  // copy of its sender/title/content strings. Declared unconditionally
  // (harmless a few bytes when unused) so the header needs no #ifdef.
  size_t notificationsDismissed = 0;
  bool hasNotificationSnapshot = false;
  uint32_t lastNotificationId = 0;
  uint8_t lastNotificationHour = 0;
  uint8_t lastNotificationMinute = 0;
  bool lastNotificationIsCall = false;

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
