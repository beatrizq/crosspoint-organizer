#pragma once

/**
 * Keeps a copy of the sleep screen wallpaper the user chose, so a feature that
 * overwrites /sleep.bmp can hand it back.
 *
 * The Organizer's Tasks tab repaints /sleep.bmp from the task list after every
 * sync (Settings -> Organizer -> Todoist -> Sleep Screen). That file is also
 * where the image viewer's "Set Cover" action writes, so switching the option
 * on used to destroy a wallpaper the user had picked, with no way back:
 * switching it off again left the task screenshot in place forever.
 *
 * The copy is taken once, the first time a task screenshot is about to replace
 * a wallpaper this module has not already preserved, and it is handed back -
 * and dropped - when the option is switched off. Choosing a new cover in the
 * image viewer drops it too: the user has just said what the wallpaper should
 * be, so an older copy is no longer theirs to restore.
 *
 * ~48KB on the SD card while it exists, which is the framebuffer written out as
 * a 1-bit BMP. Nothing is held in RAM beyond a small copy buffer.
 */
namespace SleepWallpaperBackup {

/** True when a wallpaper is being held for restore(). */
bool hasBackup();

/**
 * Copies the current /sleep.bmp aside, unless one is already held or there is
 * no wallpaper to preserve. Returns true only when a copy was just taken.
 */
bool captureIfAbsent();

/**
 * Puts the held wallpaper back and drops the copy. The copy is kept until the
 * replacement is safely in place, so a failure part-way is recoverable by
 * calling this again. Returns false when there was nothing to restore.
 */
bool restore();

/** Drops the held copy, if any: the wallpaper it preserved is no longer wanted. */
void discard();

}  // namespace SleepWallpaperBackup
