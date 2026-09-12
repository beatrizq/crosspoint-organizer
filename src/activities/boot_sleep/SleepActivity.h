#pragma once
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  // showSleepLabel: true only for the DYNAMIC mode's own call -- replaces
  // whatever the captured screenshot's own (now-stale) button-hints row was
  // showing with a plain "Sleep screen" bar. CUSTOM's user-picked wallpaper
  // and COVER_CUSTOM's book cover are real images, not screenshots, so they
  // never carry a stale hints row to begin with and pass the default false.
  void renderCustomSleepScreen(bool showSleepLabel = false) const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool showSleepLabel = false) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  // Black bar across the bottom, "Sleep screen" centered in white -- see
  // renderCustomSleepScreen()'s own doc comment for why only DYNAMIC draws
  // this. Must run before the bitmap's own displayBuffer()/
  // displayGrayscaleBase() call so it lands in the same single refresh.
  void drawSleepScreenLabel() const;

  bool fromTimeout = false;
};
