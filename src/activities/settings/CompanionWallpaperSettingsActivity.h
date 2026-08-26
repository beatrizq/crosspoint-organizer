#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/**
 * One row per companion mood (see companion::Mood), each showing the SD
 * image currently assigned to it, or "Not Set". Confirm on a row opens an
 * Options popup [Choose File, Clear] -- Choose File launches FileBrowserActivity
 * in its image-picking mode, Clear empties that mood's assignment. Reached
 * from CompanionSettingsActivity's own "Mood Wallpapers" row.
 *
 * The assignment itself is read by SleepActivity, which shows a mood's
 * wallpaper (when one is assigned) ahead of the existing random custom-pool
 * sleep screen -- see CompanionWallpaperStore.
 */
class CompanionWallpaperSettingsActivity final : public Activity {
 public:
  explicit CompanionWallpaperSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CompanionWallpaperSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  void openPicker(int moodIndex);

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;

  // Same reasoning as CompanionSettingsActivity's own swallowConfirmRelease:
  // this screen is reached on Confirm going down, so the matching release can
  // still land here once it is already active. Armed in onEnter() only when
  // Confirm is still physically down at that point.
  bool swallowConfirmRelease = false;
  // The Options popup and the file browser both answer on a button going
  // down in places, so their Confirm/Back release can land back here once
  // this screen is active again -- same idea, armed from their own results.
  bool swallowBackRelease = false;
};
