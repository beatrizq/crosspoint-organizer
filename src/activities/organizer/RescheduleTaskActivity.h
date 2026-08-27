#pragma once

#include <CivilTime.h>
#include <GfxRenderer.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * Date picker for rescheduling a Todoist task: three editable fields (day,
 * month, year), modelled directly on ClockOffsetActivity's sign/hours/minutes
 * picker. Confirm (short press) cycles the active field; Up/Down adjust it.
 * Unlike ClockOffsetActivity, which commits passively on exit because it is a
 * settings screen, this reports back via ActivityResult (DateResult) - Back
 * cancels, holding Confirm commits - since rescheduling is a real decision a
 * caller needs an explicit answer to, not a value to read whenever convenient.
 */
class RescheduleTaskActivity final : public Activity {
 public:
  // `initialDate` seeds the three fields - packed days since 2000-01-01, the
  // same form TodoistTask::dueDays uses (see CivilTime.h). Pass
  // TODOIST_TASKS.getSyncDate() converted to a packed date for "default to
  // today".
  RescheduleTaskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint16_t initialDate)
      : Activity("RescheduleTask", renderer, mappedInput), initialDate(initialDate) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  enum Field { FIELD_DAY = 0, FIELD_MONTH = 1, FIELD_YEAR = 2, FIELD_COUNT };
  Field activeField = FIELD_DAY;

  const uint16_t initialDate;
  // Working copy of the date, edited in place. Bounds match civil::packDate's
  // representable range (years 2000..2179).
  int32_t year = 2000;
  uint32_t month = 1;
  uint32_t day = 1;

  // Screen was entered with Confirm already down (e.g. still held from
  // picking "Reschedule" off the Options popup) - its eventual release must
  // not be read as a fresh press, and its already-elapsed hold time must not
  // count toward the commit gesture below.
  bool swallowConfirmRelease = false;
  // True only once a genuinely fresh Confirm press has been seen since this
  // field/screen state; gates the hold-to-commit check so a press carried
  // over from the previous screen can never auto-commit before the user has
  // seen the picker.
  bool confirmHoldArmed = false;

  void loadInitialDate();
  uint16_t packedDate() const;
  uint32_t daysInActiveMonth() const;
  void adjustActiveField(int delta);
  bool fieldFromPoint(int x, int y, Field& field) const;
};
