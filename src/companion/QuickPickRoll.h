#pragma once

#include <string>

// Weighted-random pick across pending tasks and habits, shared by
// HomeActivity (rolled once per Home visit) and QuickPickActivity's Random
// action (rerolled on demand) so both stay backed by the exact same pool and
// weights -- see lib/QuickPick for the pure selection policy this wraps.
namespace quickpick {

struct RollResult {
  std::string text;    // task content / habit name
  std::string itemId;  // Todoist task id / Habitify habit id
  bool isHabit = false;
  bool poolEmpty = true;
};

// Pool: every task due today or overdue (undated and future-dated tasks are
// left out - "give me something to do right now" is not served by a task with
// no date at all, or one due in three weeks), plus every habit not yet
// complete today. All equally likely among themselves except habits, which
// are weighted toward whichever is closest to its own target.
RollResult roll();

}  // namespace quickpick
