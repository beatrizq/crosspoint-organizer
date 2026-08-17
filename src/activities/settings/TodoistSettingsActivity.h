#pragma once

#include <string>

#include "activities/UiListActivity.h"

/**
 * Settings submenu for the Todoist integration: enter or clear the API token,
 * plus the hint for where syncing happens (the Today screen, not here).
 */
class TodoistSettingsActivity final : public UiListActivity {
 public:
  explicit TodoistSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  // API Token, Sleep Screen, Clear Token, and the non-interactive sync hint row.
  static constexpr int MENU_ITEMS = 4;

 private:
  int listCount() const override { return MENU_ITEMS; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Fixed-capacity row storage: MENU_ITEMS is a compile-time constant, so the
  // rows cost no heap. Only the token and sleep-screen values change at runtime.
  std::string tokenValue_;
  std::string sleepScreenValue_;
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
