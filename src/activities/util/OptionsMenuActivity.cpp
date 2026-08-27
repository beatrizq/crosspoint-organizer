#include "OptionsMenuActivity.h"

#include "HalDisplay.h"

void OptionsMenuActivity::onEnter() {
  Activity::onEnter();

  optionPopup.show(titleId, options, 0, [this](const int index) {
    setResult(OptionPickResult{index});
    finish();
  });

  requestUpdate(true);
}

void OptionsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void OptionsMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
