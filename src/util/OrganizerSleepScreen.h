#pragma once

#include <string>

/**
 * Installs a user-picked image as the sleep screen's Custom wallpaper.
 *
 * /sleep.bmp is what SleepActivity's CUSTOM (and DYNAMIC, which renders the
 * same way -- see SleepActivity::renderCustomSleepScreen()) mode displays.
 * This is the write side of it for a file the user explicitly chose, used by
 * the image viewer's "Set Cover" action.
 *
 * Used to also cover an organizer app repainting /sleep.bmp on its own,
 * opportunistically, whenever its contents changed (Settings -> Organizer ->
 * Sleep Screen App) -- replaced by DYNAMIC mode capturing whatever screen the
 * device actually was on right before sleeping (see
 * ActivityManager::goToSleep()), so that mechanism and the backup/revert
 * machinery it needed (to hand a picked wallpaper back after an app
 * screenshot had overwritten it) are gone rather than dead code kept around.
 */
namespace organizerSleepScreen {

/**
 * Copies sourcePath to /sleep.bmp as the sleep wallpaper and switches the
 * sleep mode to CUSTOM.
 *
 * Returns false, leaving /sleep.bmp untouched, if sourcePath could not be read
 * or the copy failed part-way.
 */
bool installCustomWallpaper(const std::string& sourcePath);

}  // namespace organizerSleepScreen
