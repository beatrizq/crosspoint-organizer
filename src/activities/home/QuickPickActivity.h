#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * Full-screen "companion reveals what it picked for you" screen: the
 * character, scaled up, holding one line -- the task or habit text Home's
 * quick-pick chose, or an empty-pool message when there was nothing to pick
 * from. Reached from Home (companion focused, then activated) or reconstructed
 * on boot from CrossPointState when the device was showing this screen at the
 * moment it went to sleep -- either way, everything it needs comes through the
 * constructor, since it also mirrors its own content into CrossPointState on
 * entry and on every reroll (see onEnter()/reroll()) rather than main.cpp
 * fishing it out reactively.
 *
 * Random rerolls the pick in place; Go opens the same Options menu Tasks and
 * Habits offer on a row -- Complete/Log, or Focus session -- acting on the
 * suggested item directly rather than navigating there; Back returns to Home,
 * reporting whatever is currently held as a QuickPickResult so Home's own
 * bubble can be kept in sync. setResult() has to be called before finish(),
 * not in onExit() -- ActivityManager::popActivity() reads the result before
 * it runs the outgoing activity's onExit().
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

  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;

  // See OrganizerScreenActivity's own swallow flags for why these exist: the
  // Options popup (and the confirmation or number entry it can lead to)
  // answers on a button press, not its release, and that release is still
  // owed to this screen once the sub-activity it was pushed from closes.
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;
};
