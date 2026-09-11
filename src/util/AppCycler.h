#pragma once

#include "util/HomeAppOrder.h"

/**
 * Side Up/Down -> previous/next app, on every screen one of Home's own tiles
 * opens (Read, Tasks, Calendar, Budget, Habits, Alerts).
 *
 * The side buttons are physically fixed (never remapped, unlike the front
 * four -- see MappedInputManager::Button's own doc comment) and otherwise
 * unused once inside one of these screens, where Up/Down already means
 * something else on the front buttons (row paging). Cycling through the
 * user's own App Order this way means a side press always lands on a sibling
 * app's real screen -- the same one its Home tile opens, in the same order
 * App Order set for the grid -- rather than a separate, parallel navigation
 * concept.
 */
namespace appCycler {

// The app after/before `current` in the user's own App Order (same order and
// gating -- skipping Notifications outside the BLE spike build -- as
// HomeActivity::buildEntries()), wrapping.
homeAppOrder::AppId nextApp(homeAppOrder::AppId current);
homeAppOrder::AppId previousApp(homeAppOrder::AppId current);

// Replaces the current activity with the given app's own screen, the same
// way tapping its Home tile does (ActivityManager::goTo*()).
void open(homeAppOrder::AppId app);

}  // namespace appCycler
