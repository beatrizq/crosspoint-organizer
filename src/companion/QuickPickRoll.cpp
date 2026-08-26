#include "QuickPickRoll.h"

#include <HabitifyHabitCache.h>
#include <QuickPick.h>
#include <TodoistTaskCache.h>
#include <esp_random.h>

#include <vector>

namespace quickpick {

RollResult roll() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  const bool knowToday = !TODOIST_TASKS.getSyncDate().empty();
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  std::vector<WeightedItem> candidates;
  candidates.reserve(tasks.size());
  for (size_t i = 0; i < tasks.size(); i++) {
    // Overdue or due today only - not undated (nothing says it needs doing
    // right now) and not a future date. Today unknown excludes everything
    // dated, since neither overdue nor due-today can be told apart from it.
    const bool dueTodayOrOverdue = knowToday && (tasks[i].overdue || tasks[i].dueDays == today);
    if (!dueTodayOrOverdue) continue;
    candidates.push_back({static_cast<int>(i), false, 1});
  }

  const auto& habits = HABITIFY_HABITS.getHabits();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].isComplete()) continue;
    candidates.push_back({static_cast<int>(i), true, habitWeight(habits[i].shownCurrent(), habits[i].target)});
  }

  const uint32_t total = totalWeight(candidates);
  const int picked = total == 0 ? -1 : pick(candidates, esp_random() % total);

  RollResult result;
  if (picked < 0) return result;  // poolEmpty stays true

  const auto& item = candidates[static_cast<size_t>(picked)];
  result.poolEmpty = false;
  result.isHabit = item.isHabit;
  if (item.isHabit) {
    result.text = habits[static_cast<size_t>(item.sourceIndex)].name;
    result.itemId = habits[static_cast<size_t>(item.sourceIndex)].id;
  } else {
    result.text = tasks[static_cast<size_t>(item.sourceIndex)].content;
    result.itemId = tasks[static_cast<size_t>(item.sourceIndex)].id;
  }
  return result;
}

}  // namespace quickpick
