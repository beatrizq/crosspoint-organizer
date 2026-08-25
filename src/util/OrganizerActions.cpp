#include "OrganizerActions.h"

#include <HabitifyHabitCache.h>
#include <Logging.h>
#include <TodoistTaskCache.h>

#include "companion/CompanionTracker.h"

namespace organizerActions {

void completeTask(const size_t cacheIndex) {
  if (cacheIndex >= TODOIST_TASKS.getTasks().size()) return;

  LOG_DBG("ORGACT", "Completing task: %s", TODOIST_TASKS.getTasks()[cacheIndex].content.c_str());
  // Queued locally and pushed on the next sync, so completing works with the
  // radio off; the row leaves the list immediately either way.
  TODOIST_TASKS.completeTaskAt(cacheIndex);
  TODOIST_TASKS.saveToFile();
  // A completion is one of the two things the companion reacts to; credit it
  // immediately rather than waiting for the next sync or Home visit.
  COMPANION.recordActivity();
}

void logHabit(const size_t cacheIndex, const float amount) {
  if (cacheIndex >= HABITIFY_HABITS.getHabits().size()) return;
  if (amount <= 0.0f) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (habits[cacheIndex].unitSymbol.empty()) return;

  LOG_DBG("ORGACT", "+%g to %s", static_cast<double>(amount), habits[cacheIndex].name.c_str());
  HABITIFY_HABITS.addPending(cacheIndex, amount);
  HABITIFY_HABITS.saveToFile();
  // A completion is one of the two things the companion reacts to; credit it
  // immediately in case this press is what pushed a habit to isComplete().
  COMPANION.recordActivity();
}

}  // namespace organizerActions
