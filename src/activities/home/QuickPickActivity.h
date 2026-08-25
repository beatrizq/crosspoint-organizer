#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * Full-screen "companion reveals what it picked for you" screen: the
 * character, scaled up, holding one line -- the task or habit text Home's
 * quick-pick chose, or an empty-pool message when there was nothing to pick
 * from. Reached only from Home (companion focused, then activated), so it
 * takes what it needs through the constructor rather than any persisted
 * state.
 */
class QuickPickActivity final : public Activity {
 public:
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pickedText, const bool isHabit,
                    const bool poolEmpty)
      : Activity("QuickPick", renderer, mappedInput),
        pickedText(std::move(pickedText)),
        isHabit(isHabit),
        poolEmpty(poolEmpty) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string pickedText;
  bool isHabit;
  bool poolEmpty;
};
