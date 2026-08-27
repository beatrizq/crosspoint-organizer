#include "OrganizerSleepScreen.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "util/ScreenshotUtil.h"
#include "util/SleepWallpaperBackup.h"

namespace organizerSleepScreen {
namespace {

// The sleep screen SleepActivity renders in CUSTOM mode, and the file the image
// viewer's "Set Cover" action and installCustomWallpaper() write.
constexpr char SLEEP_SCREEN_PATH[] = "/sleep.bmp";

// One SD block-aligned chunk, on the heap rather than the stack for the same
// reason as SleepWallpaperBackup's own copy buffer: 2KB is an order of magnitude
// past what a task stack here should carry.
constexpr size_t COPY_CHUNK = 2048;

// The setting value that selects a given app, or SLEEP_APP_OFF for one that
// cannot be chosen. Read is not an organizer app and has no screen to snapshot.
uint8_t settingValueFor(const homeAppOrder::AppId id) {
  switch (id) {
    case homeAppOrder::AppId::Tasks:
      return CrossPointSettings::SLEEP_APP_TASKS;
    case homeAppOrder::AppId::Calendar:
      return CrossPointSettings::SLEEP_APP_CALENDAR;
    case homeAppOrder::AppId::Budget:
      return CrossPointSettings::SLEEP_APP_BUDGET;
    case homeAppOrder::AppId::Habits:
      return CrossPointSettings::SLEEP_APP_HABITS;
    case homeAppOrder::AppId::Read:
      break;
  }
  return CrossPointSettings::SLEEP_APP_OFF;
}

// Switches the sleep mode to CUSTOM, remembering the mode it replaces - and only
// the first time, so a later call does not record CUSTOM itself as the mode to
// go back to. No-op once already CUSTOM.
void switchToCustomMode() {
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) return;
  if (SETTINGS.previousSleepScreenMode == CrossPointSettings::NO_PREVIOUS_SLEEP_SCREEN) {
    SETTINGS.previousSleepScreenMode = SETTINGS.sleepScreen;
  }
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  SETTINGS.saveToFile();
  LOG_INF("OSLEEP", "Sleep screen mode switched to custom");
}

}  // namespace

bool isChosen(const homeAppOrder::AppId id) {
  const uint8_t wanted = settingValueFor(id);
  if (wanted == CrossPointSettings::SLEEP_APP_OFF) return false;
  return SETTINGS.organizerSleepApp == wanted;
}

bool hasBackup() { return SleepWallpaperBackup::hasBackup(); }

void capture(const GfxRenderer& renderer) {
  const uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!framebuffer) {
    LOG_ERR("OSLEEP", "Framebuffer unavailable; sleep screen not updated");
    return;
  }

  // Whatever wallpaper is there is the user's until this replaces it, so it is
  // copied aside first - once, on the first replacement - and handed back when
  // the option goes to Off. Without it, switching the option on destroyed a
  // chosen wallpaper and switching it off left an app screenshot in its place.
  SleepWallpaperBackup::captureIfAbsent();

  // Same file and format the "Set Cover" action writes from the image viewer, so
  // SleepActivity's CUSTOM mode picks it up unchanged.
  if (!ScreenshotUtil::saveFramebufferAsBmp(SLEEP_SCREEN_PATH, framebuffer, renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR("OSLEEP", "Failed to write %s", SLEEP_SCREEN_PATH);
    return;
  }
  LOG_DBG("OSLEEP", "Sleep screen updated from an organizer screen");

  // The wallpaper is only shown in CUSTOM mode; switching is what makes the
  // snapshot visible at all.
  switchToCustomMode();
}

bool installCustomWallpaper(const std::string& sourcePath) {
  bool success = false;
  HalFile inFile, outFile;
  if (Storage.openFileForRead("OSLEEP", sourcePath, inFile)) {
    if (Storage.openFileForWrite("OSLEEP", SLEEP_SCREEN_PATH, outFile)) {
      auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_CHUNK);
      if (buffer) {
        success = true;
        int bytesRead;
        while ((bytesRead = inFile.read(buffer.get(), COPY_CHUNK)) > 0) {
          if (outFile.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            LOG_ERR("OSLEEP", "Short write installing %s", sourcePath.c_str());
            success = false;
            break;
          }
        }
      } else {
        LOG_ERR("OSLEEP", "OOM: %zu bytes", COPY_CHUNK);
      }
      outFile.flush();
    }
  }

  if (!success) {
    LOG_ERR("OSLEEP", "Failed to install %s as the sleep wallpaper", sourcePath.c_str());
    return false;
  }

  // The user has just said what the wallpaper should be, so any copy an
  // organizer app's screenshot was holding is no longer theirs to put back.
  SleepWallpaperBackup::discard();
  switchToCustomMode();
  LOG_INF("OSLEEP", "Sleep screen wallpaper installed from %s", sourcePath.c_str());
  return true;
}

bool revert() {
  // The file first: it is the part the user cares about keeping, and the one they
  // may have transferred to the device themselves.
  const bool restored = SleepWallpaperBackup::hasBackup() && SleepWallpaperBackup::restore();

  const uint8_t previous = SETTINGS.previousSleepScreenMode;
  if (previous != CrossPointSettings::NO_PREVIOUS_SLEEP_SCREEN) {
    // Guarded: a corrupt settings file must not put an out-of-range mode into
    // play.
    if (previous < CrossPointSettings::SLEEP_SCREEN_MODE::SLEEP_SCREEN_MODE_COUNT) {
      SETTINGS.sleepScreen = previous;
      LOG_INF("OSLEEP", "Sleep screen mode restored to %u", static_cast<unsigned>(previous));
    }
    SETTINGS.previousSleepScreenMode = CrossPointSettings::NO_PREVIOUS_SLEEP_SCREEN;
  }
  // Saved whether or not a mode was restored: organizerSleepApp itself has just
  // changed, and the caller should not have to remember to persist it.
  SETTINGS.saveToFile();
  return restored;
}

}  // namespace organizerSleepScreen
