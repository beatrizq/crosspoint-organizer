#include "OrganizerSleepScreen.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "util/ScreenshotUtil.h"
#include "util/SleepWallpaperBackup.h"

namespace organizerSleepScreen {
namespace {

// The sleep screen SleepActivity renders in CUSTOM mode, and the file the image
// viewer's "Set Cover" action writes.
constexpr char SLEEP_SCREEN_PATH[] = "/sleep.bmp";

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

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) return;

  // Remembered before it is overwritten, and only the first time: a later
  // snapshot would otherwise record CUSTOM as the mode to go back to.
  if (SETTINGS.previousSleepScreenMode == CrossPointSettings::NO_PREVIOUS_SLEEP_SCREEN) {
    SETTINGS.previousSleepScreenMode = SETTINGS.sleepScreen;
  }
  // The wallpaper is only shown in CUSTOM mode; switching is what makes the
  // snapshot visible at all.
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  SETTINGS.saveToFile();
  LOG_INF("OSLEEP", "Sleep screen mode switched to custom");
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
