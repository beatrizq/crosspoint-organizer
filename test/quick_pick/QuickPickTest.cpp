#include <gtest/gtest.h>

#include "QuickPick/QuickPick.h"

using quickpick::WeightedItem;

// -------------------------------------------------------------- total/pick

TEST(QuickPick, EmptyListHasNoTotalAndPicksNothing) {
  const std::vector<WeightedItem> items;
  EXPECT_EQ(quickpick::totalWeight(items), 0u);
  EXPECT_EQ(quickpick::pick(items, 0), -1);
}

TEST(QuickPick, SingleItemAlwaysPicked) {
  const std::vector<WeightedItem> items = {{0, false, 3}};
  EXPECT_EQ(quickpick::totalWeight(items), 3u);
  EXPECT_EQ(quickpick::pick(items, 0), 0);
  EXPECT_EQ(quickpick::pick(items, 2), 0);
}

TEST(QuickPick, DrawSelectsByCumulativeRange) {
  // Weights 2, 1, 3 -> ranges [0,2), [2,3), [3,6).
  const std::vector<WeightedItem> items = {{10, false, 2}, {11, false, 1}, {12, true, 3}};
  EXPECT_EQ(quickpick::totalWeight(items), 6u);
  EXPECT_EQ(quickpick::pick(items, 0), 0);
  EXPECT_EQ(quickpick::pick(items, 1), 0);
  EXPECT_EQ(quickpick::pick(items, 2), 1);
  EXPECT_EQ(quickpick::pick(items, 3), 2);
  EXPECT_EQ(quickpick::pick(items, 5), 2);
}

TEST(QuickPick, DrawAtOrPastTotalClampsToLastItem) {
  const std::vector<WeightedItem> items = {{0, false, 2}, {1, false, 3}};
  EXPECT_EQ(quickpick::pick(items, 5), 1);    // exactly total
  EXPECT_EQ(quickpick::pick(items, 999), 1);  // well past total
}

TEST(QuickPick, PickedItemCarriesCallerSourceInfo) {
  const std::vector<WeightedItem> items = {{7, true, 1}};
  const int idx = quickpick::pick(items, 0);
  ASSERT_GE(idx, 0);
  EXPECT_EQ(items[static_cast<size_t>(idx)].sourceIndex, 7);
  EXPECT_TRUE(items[static_cast<size_t>(idx)].isHabit);
}

// --------------------------------------------------------------- habitWeight

TEST(QuickPickHabitWeight, UntouchedHabitMatchesTaskBaseline) { EXPECT_EQ(quickpick::habitWeight(0.0f, 10.0f), 1u); }

TEST(QuickPickHabitWeight, NearlyDoneHabitWeighsMoreThanUntouched) {
  const uint32_t untouched = quickpick::habitWeight(0.0f, 10.0f);
  const uint32_t nearlyDone = quickpick::habitWeight(9.0f, 10.0f);
  EXPECT_GT(nearlyDone, untouched);
}

TEST(QuickPickHabitWeight, AtOrPastTargetCapsRatherThanGrowingUnbounded) {
  EXPECT_EQ(quickpick::habitWeight(10.0f, 10.0f), quickpick::habitWeight(20.0f, 10.0f));
}

TEST(QuickPickHabitWeight, ZeroOrNegativeTargetIsSafe) {
  EXPECT_EQ(quickpick::habitWeight(5.0f, 0.0f), 1u);
  EXPECT_EQ(quickpick::habitWeight(5.0f, -1.0f), 1u);
}

TEST(QuickPickHabitWeight, NegativeCurrentIsSafe) {
  // Should not happen in practice, but must not underflow into a huge weight.
  EXPECT_EQ(quickpick::habitWeight(-5.0f, 10.0f), 1u);
}
