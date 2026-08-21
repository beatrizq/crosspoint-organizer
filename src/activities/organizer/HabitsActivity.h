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
 * Select adds one to the selected habit, after a confirmation prompt - the same
 * gesture and the same prompt-then-act shape as completing a Todoist task, so the
 * two screens behave alike. It is deliberately not Habitify's own "mark complete"
 * endpoint, which fills the whole goal at once: the point of showing 1/3 is being
 * able to log the second and the third separately. The press only touches the
 * cache, so it works with the radio off and the number moves immediately; the
 * accumulated amount is pushed on the next sync, one request per habit however
 * many times Select was pressed, before the journal is re-fetched.
 *
 * Today is the only tab. It stays as a tab so the screen reads as a sibling of
 * Tasks, Calendar and Budget rather than as a bare list, and because progress is
 * per-day: naming the tab Today is what says the counts reset.
 */
class HabitsActivity final : public OrganizerScreenActivity {
 public:
  enum class Tab : uint8_t { TODAY = 0 };
  static constexpr int TAB_COUNT = 1;

  explicit HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : OrganizerScreenActivity("Habits", renderer, mappedInput, static_cast<int>(Tab::TODAY)) {}

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

  // Asks first; performIncrement() is what actually moves the number.
  void completeSelectedHabit();
  void performIncrement(int cacheIndex);
  void performSync();
  // Renders progress as "x/y", or as a bare count for a habit with no goal.
  void formatProgress(const HabitifyHabit& habit, char* out, size_t outSize) const;
};
