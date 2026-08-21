#pragma once

#include "util/HomeAppOrder.h"

class GfxRenderer;

/**
 * Paints the sleep screen from one organizer app's first tab.
 *
 * Settings -> Organizer -> Sleep Screen App picks which app, or Off. Whenever
 * that app's contents change - a sync, or a change made on the device with the
 * radio off, like completing a task or logging a habit - its screen is
 * snapshotted to /sleep.bmp and the sleep mode is switched to CUSTOM so the
 * snapshot is what a sleeping device shows.
 *
 * This began as a Todoist-only option and was general all along in everything but
 * its wiring: the snapshot is just the framebuffer, so any of the four screens can
 * feed it.
 *
 * Only the first tab is used. It is the tab the screen opens on, so it is the one
 * the user would recognise, and a sleep screen that changed shape depending on
 * which tab happened to be open last would be worse than one that did not.
 *
 * Reverting is the point of the backup underneath. /sleep.bmp is also where the
 * image viewer's "Set Cover" writes, so it may hold a wallpaper the user
 * transferred to the device themselves and never wants to lose. That file is
 * copied aside before the first snapshot replaces it and handed back when the
 * option goes to Off - see SleepWallpaperBackup.
 */
namespace organizerSleepScreen {

/** Whether this app is the one currently feeding the sleep screen. */
bool isChosen(homeAppOrder::AppId id);

/**
 * Snapshots the framebuffer as the sleep wallpaper and switches the sleep mode to
 * CUSTOM, remembering the mode it replaced.
 *
 * The caller must already have repainted and waited, because what gets written is
 * whatever the framebuffer holds right now.
 */
void capture(const GfxRenderer& renderer);

/**
 * Puts the user's own wallpaper and sleep mode back, and drops the copy.
 *
 * Returns true when a wallpaper was restored, so a caller can say so. Safe to
 * call with nothing held: reverting twice is not an error.
 */
bool revert();

/** Whether revert() has a wallpaper to hand back, for a caller that wants to say so. */
bool hasBackup();

}  // namespace organizerSleepScreen
