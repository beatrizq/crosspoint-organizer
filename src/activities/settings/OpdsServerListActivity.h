#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Activity showing the list of configured OPDS servers.
 * Allows adding new servers and editing/deleting existing ones.
 * When pickerMode is true, selecting a server navigates to the OPDS browser
 * instead of opening the editor (used from the home screen).
 */
class OpdsServerListActivity final : public Activity {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false,
                                  bool returnToReadMenu = false)
      : Activity("OpdsServerList", renderer, mappedInput), pickerMode(pickerMode), returnToReadMenu(returnToReadMenu) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool pickerMode = false;
  // pickerMode only: when set, Back returns to ReadMenuActivity instead of
  // Home -- set only by ActivityManager::goToBrowser() on behalf of
  // ReadMenuActivity.
  bool returnToReadMenu = false;

  int getItemCount() const;
  void handleSelection();
};
