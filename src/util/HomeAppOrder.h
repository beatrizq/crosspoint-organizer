#pragma once
#include <I18n.h>

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

/**
 * The apps on the home grid, and the order the user put them in.
 *
 * One table describes each app once - its persisted id, the home entry it opens,
 * its label and its artwork - so HomeActivity and the App Order screen cannot
 * disagree about what exists or what it is called.
 *
 * The order is persisted as a short string of digits in
 * CrossPointSettings::homeAppOrder, one digit per app id ("01234" is the default
 * left-to-right). A string rather than a packed array because the settings file
 * and the web API already carry char fields, so it needs no new persistence.
 *
 * Parsing is deliberately forgiving. A stored order may name apps that no longer
 * exist, omit apps added since it was written, or be hand-edited into nonsense;
 * in every case the result is still a complete permutation of the current apps,
 * because an unreadable value should cost the user their arrangement, never their
 * home screen.
 */
namespace homeAppOrder {

/**
 * Stable per-app id. These are written to the settings file, so the numbers are
 * part of the format: append new apps, never renumber or reuse.
 */
enum class AppId : uint8_t {
  Read = 0,
  Tasks = 1,
  Calendar = 2,
  Budget = 3,
  Habits = 4,
};

constexpr int APP_COUNT = 5;

struct AppInfo {
  AppId id;
  StrId label;
  UIIcon icon;
};

// Which home entry the tile opens is deliberately not in this table: it lives in
// ActivityManager.h, whose unique_ptr<Activity> members cannot be instantiated
// without Activity.h, and a util header has no business dragging the activity
// layer in behind it. HomeActivity maps AppId to its own HomeMenuItem instead.

/** The apps in their built-in order, indexed by AppId. */
const AppInfo& appAt(int index);

/** The default order string, used when nothing is stored or it is unusable. */
constexpr const char* DEFAULT_ORDER = "01234";

// Room for one digit per app plus the terminator. Sized with headroom so adding
// apps does not need a settings-file migration.
constexpr size_t ORDER_MAX_LEN = 16;

/**
 * Reads the stored order into `out`, which receives exactly APP_COUNT indices
 * into the app table.
 *
 * Known ids are taken in the order they appear; any app the string does not
 * mention is appended in built-in order, and anything unrecognised or repeated is
 * ignored. So a truncated, stale or corrupt value still yields every app exactly
 * once.
 */
void parse(const char* stored, int (&out)[APP_COUNT]);

/** Renders an order back to the stored form. `outSize` must be >= APP_COUNT + 1. */
void format(const int (&order)[APP_COUNT], char* out, size_t outSize);

}  // namespace homeAppOrder
