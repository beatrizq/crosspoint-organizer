#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

// Sleep window boundary picker (start or end), modelled directly on
// ClockOffsetActivity's hours/minutes picker minus the sign field: two
// editable fields (hours 0-23, minutes 0-59); Confirm cycles fields, -/+
// adjust the active one by 1. A settings screen, so it commits passively on
// exit the same way ClockOffsetActivity does, rather than reporting back via
// ActivityResult.
class CompanionSleepTimeActivity final : public Activity {
 public:
  // isStart selects which pair of SETTINGS fields this instance edits --
  // companionSleepStartHour/Minute when true, companionSleepEndHour/Minute
  // when false -- and which header string is shown.
  CompanionSleepTimeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool isStart)
      : Activity("CompanionSleepTime", renderer, mappedInput), isStart(isStart) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  enum Field { FIELD_HOURS = 0, FIELD_MINUTES = 1, FIELD_COUNT };
  Field activeField = FIELD_HOURS;

  const bool isStart;

  // Working copy, edited in-place. Saved back to SETTINGS on exit.
  uint8_t hours = 0;
  uint8_t minutes = 0;

  void loadFromSettings();
  void saveToSettings() const;
  void adjustActiveField(int delta);
  bool fieldFromPoint(int x, int y, Field& field) const;
  void getTouchControlRects(Rect& minusRect, Rect& plusRect) const;
  // "Front buttons: 1" / "Side buttons: 5" -- same legend and wording
  // IntervalSelectionActivity uses for its own front/side step split.
  void drawStepHintLine(int y, StrId labelId, int step) const;
};
