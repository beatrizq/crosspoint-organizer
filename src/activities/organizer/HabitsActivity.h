#pragma once
#include <HabitifyClient.h>

#include <cstddef>
#include <string>

#include "OrganizerScreenActivity.h"

/**
 * The Habits screen: today's habits and how far each has got.
 *
 * A row is the habit's name with its progress hard right as "x/y" - 1/3 - the
 * same layout the Budget screen gives a category and its balance, because it
 * answers the same shape of question at a glance.
 *
 * Select opens a number entry for the selected habit, defaulting to 1 so a
 * single Confirm press behaves exactly like the old one-tap "+1". It is
 * deliberately not Habitify's own "mark complete" endpoint, which fills the
 * whole goal at once: the point of showing 1/3 is being able to log the
 * second and the third separately, in whatever amount was actually done. The
 * entry only touches the cache, so it works with the radio off and the number
 * moves immediately; the accumulated amount is pushed on the next sync, one
 * request per habit however many separate amounts were logged in between,
 * before the journal is re-fetched.
 *
 * Today is the only tab. It stays as a tab so the screen reads as a sibling of
 * Tasks, Calendar and Budget rather than as a bare list, and because progress is
 * per-day: naming the tab Today is what says the counts reset.
 */
class HabitsActivity final : public OrganizerScreenActivity {
 public:
  enum class Tab : uint8_t { TODAY = 0 };
  static constexpr int TAB_COUNT = 1;

  // selectHabitId, when non-empty, selects that habit's row on first paint
  // instead of row 0. One-shot -- cleared once applied.
  explicit HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string selectHabitId = "")
      : OrganizerScreenActivity("Habits", renderer, mappedInput, static_cast<int>(Tab::TODAY)),
        selectHabitId(std::move(selectHabitId)) {}

  void onEnter() override;

 protected:
  const char* screenTitle() const override;
  int tabCount() const override { return TAB_COUNT; }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  const char* rowConfirmLabel() const override;
  void onRowConfirm() override;
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::HABITS; }
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Habits; }

 private:
  // The cache index behind a visible row, or -1. Only the "hide completed"
  // setting makes these differ, so the scan is over at most HABITIFY_MAX_HABITS.
  int cacheIndexForRow(int row) const;
  bool isVisible(size_t cacheIndex) const;

  // See the constructor comment. Consumed and cleared in onEnter().
  std::string selectHabitId;

  // Select opens this first, rather than completeSelectedHabit() directly --
  // see rowConfirmLabel()/onRowConfirm().
  void showRowOptions();
  // Opens the number entry; performIncrement() is what actually moves the number.
  void completeSelectedHabit();
  void performIncrement(int cacheIndex, float amount);
  void performSync();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the same habit.
  void offerFocusSession(int cacheIndex);
  // Renders progress as "x/y", or as a bare count for a habit with no goal.
  void formatProgress(const HabitifyHabit& habit, char* out, size_t outSize) const;
};
