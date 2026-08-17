#include <gtest/gtest.h>

#include "TodoistDate.h"

using todoist::DUE_NONE;
using todoist::dueDaysFromIso;
using todoist::isoFromDueDays;

namespace {

std::string iso(const uint16_t due) {
  char buf[11];
  isoFromDueDays(due, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST(TodoistDate, EpochIsZero) { EXPECT_EQ(0, dueDaysFromIso("2000-01-01")); }

TEST(TodoistDate, RoundTripsPlainDates) {
  const char* dates[] = {"2000-01-01", "2000-03-01", "2024-02-29", "2026-08-17", "2100-12-31", "2179-01-01"};
  for (const char* d : dates) {
    const uint16_t packed = dueDaysFromIso(d);
    ASSERT_NE(DUE_NONE, packed) << d;
    EXPECT_EQ(std::string(d), iso(packed)) << d;
  }
}

TEST(TodoistDate, OrdersChronologically) {
  // The sort key and the "newest due date" derivation both rely on this.
  EXPECT_LT(dueDaysFromIso("2026-08-16"), dueDaysFromIso("2026-08-17"));
  EXPECT_LT(dueDaysFromIso("2025-12-31"), dueDaysFromIso("2026-01-01"));
  EXPECT_LT(dueDaysFromIso("2026-02-28"), dueDaysFromIso("2026-03-01"));
}

TEST(TodoistDate, ConsecutiveDaysDifferByOne) {
  EXPECT_EQ(dueDaysFromIso("2026-08-17"), dueDaysFromIso("2026-08-16") + 1);
  // Across a month boundary, a leap day, and a year boundary.
  EXPECT_EQ(dueDaysFromIso("2026-09-01"), dueDaysFromIso("2026-08-31") + 1);
  EXPECT_EQ(dueDaysFromIso("2024-02-29"), dueDaysFromIso("2024-02-28") + 1);
  EXPECT_EQ(dueDaysFromIso("2024-03-01"), dueDaysFromIso("2024-02-29") + 1);
  EXPECT_EQ(dueDaysFromIso("2027-01-01"), dueDaysFromIso("2026-12-31") + 1);
}

TEST(TodoistDate, AcceptsDatetimeByReadingOnlyTheDate) {
  // Todoist sends a full datetime for tasks with a time; only the date counts.
  EXPECT_EQ(dueDaysFromIso("2026-08-17"), dueDaysFromIso("2026-08-17T09:30:00"));
  EXPECT_EQ(dueDaysFromIso("2026-08-17"), dueDaysFromIso("2026-08-17T09:30:00.000000Z"));
}

TEST(TodoistDate, RejectsMalformedInput) {
  EXPECT_EQ(DUE_NONE, dueDaysFromIso(""));
  EXPECT_EQ(DUE_NONE, dueDaysFromIso(nullptr));
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026-08"));     // truncated
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026/08/17"));  // wrong separators
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("20260817"));    // no separators
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("20x6-08-17"));  // non-digit
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026-13-01"));  // month out of range
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026-00-01"));  // month zero
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026-08-32"));  // day out of range
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("2026-08-00"));  // day zero
  EXPECT_EQ(DUE_NONE, dueDaysFromIso("1999-12-31"));  // before the epoch
}

TEST(TodoistDate, UndatedRendersEmpty) { EXPECT_EQ("", iso(DUE_NONE)); }

TEST(TodoistDate, UndatedSortsAfterEveryRealDate) {
  // setTasks relies on this so undated tasks land at the end of the list and are
  // never picked as the newest due date.
  EXPECT_GT(DUE_NONE, dueDaysFromIso("2179-01-01"));
}

TEST(TodoistDate, TooSmallBufferYieldsEmptyRatherThanTruncatedDate) {
  char buf[4] = {'x', 'x', 'x', 'x'};
  isoFromDueDays(dueDaysFromIso("2026-08-17"), buf, sizeof(buf));
  EXPECT_STREQ("", buf);
}
