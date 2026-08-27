#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

/**
 * Generic "pick one of these" popup, as a pushable activity rather than a
 * component a screen has to wire into its own loop()/render().
 *
 * Same shape as ConfirmationActivity, generalised from a fixed Cancel/Confirm
 * pair to an arbitrary translated option list. The caller reads which index
 * was picked from the result; Back or a tap outside the dialog reports
 * isCancelled instead, exactly as ConfirmationActivity's own Cancel option
 * would, and picks nothing.
 */
class OptionsMenuActivity final : public Activity {
 public:
  OptionsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StrId titleId,
                      std::vector<std::string> options)
      : Activity("OptionsMenu", renderer, mappedInput), titleId(titleId), options(std::move(options)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  StrId titleId;
  std::vector<std::string> options;
  OptionPopup optionPopup;
};
