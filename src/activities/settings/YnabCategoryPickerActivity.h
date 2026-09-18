#pragma once
#include <YnabClient.h>

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Picks which of the budget's categories feed the Organizer's Budget tab.
 *
 * The list is fetched live rather than cached: categories are added and
 * archived as a budget evolves, this screen is only ever opened deliberately,
 * and a stale list would silently drop one the user had just created.
 * Selection is stored as category ids in YnabStore.
 */
class YnabCategoryPickerActivity final : public Activity {
 public:
  explicit YnabCategoryPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("YnabCategoryPicker", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LOADING,  // Fetching the category list
    LIST,
    FAILED,
  };

  void fetchCategories();
  void toggleSelected();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::LOADING;
  const char* statusMessage = nullptr;
  std::vector<YnabClient::CategoryInfo> categories;
  bool dirty = false;  // Selection changed and needs persisting on exit
  bool wifiActivated = false;
};
