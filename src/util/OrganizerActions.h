#pragma once
#include <cstddef>

/**
 * The cache-mutating core of "complete a task" / "log a habit", shared by
 * TasksActivity/HabitsActivity's own row action and QuickPickActivity's
 * Options menu acting on the companion's suggested item.
 *
 * Each screen still owns what is particular to it - TasksActivity's tab
 * rebuild and selection clamp, HabitsActivity's requestUpdate(), either's
 * updateSleepScreen() - this only holds the three steps that must not drift
 * out of sync between call sites: mutate the cache, persist it, and credit
 * the companion.
 *
 * Callers are responsible for holding a RenderLock across the call, exactly
 * as they already do around TodoistTaskCache/HabitifyHabitCache mutations
 * made directly.
 */
namespace organizerActions {

// Drops the task at `cacheIndex` and queues it for the next sync. No-op for
// an out-of-range index.
void completeTask(size_t cacheIndex);

// Adds `amount` to the habit at `cacheIndex`'s unpushed progress. No-op for
// an out-of-range index, a non-positive amount, or a habit with no unit (see
// HabitifyHabit::unitSymbol -- nothing to log against a goal-less habit).
void logHabit(size_t cacheIndex, float amount);

}  // namespace organizerActions
