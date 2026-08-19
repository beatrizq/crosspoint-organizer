#pragma once
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

/**
 * The Read tile's screen: the ways into a book, in one list.
 *
 * The home screen is four tiles and a cover card, which leaves no room for the
 * three library destinations - and they are a set: browse the card, pick up
 * something recent, or send a file over. They live together here rather than
 * competing with Tasks, Calendar and Budget for a tile.
 *
 * The cover card on the home screen still opens the last book directly, so this
 * screen is for everything except the obvious thing.
 */
class ReadMenuActivity final : public Activity {
 public:
  explicit ReadMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Entry {
    const char* label;  // Translated
    UIIcon icon;
    HomeMenuItem item;
  };

  // Built in onEnter: the list is walked every loop() and must not allocate
  // there. Only the OPDS entry is conditional.
  void buildEntries();
  void activateSelected();

  ButtonNavigator buttonNavigator;
  std::vector<Entry> entries;
  int selectedIndex = 0;
};
