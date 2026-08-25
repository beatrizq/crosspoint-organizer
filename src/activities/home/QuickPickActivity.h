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
 * entry (see onEnter()) rather than main.cpp fishing it out reactively.
 */
class QuickPickActivity final : public Activity {
 public:
  // itemId is the Todoist task id / Habitify habit id behind pickedText, so
  // Confirm can jump straight to that row in Tasks/Habits instead of just the
  // screen. Empty when poolEmpty is true (nothing was picked).
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pickedText,
                    std::string itemId, const bool isHabit, const bool poolEmpty)
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
  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;
};
