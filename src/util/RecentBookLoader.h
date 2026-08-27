#pragma once

#include <vector>

struct RecentBook;

/**
 * The filtering step of RECENT_BOOKS' own list -- for ReadMenuActivity's
 * leading "continue reading" row. Deliberately separate from
 * HomeActivity::loadRecentBooks(), which still exists and is still what
 * feeds a cover-tile theme's own Home display (see BaseTheme): that one also
 * carries HomeActivity's cover-buffer bookkeeping, which this has no
 * equivalent of and does not need.
 */
namespace recentBookLoader {

// The most recent `maxBooks` books whose file still exists on SD, in
// RECENT_BOOKS' own order (most recent first).
std::vector<RecentBook> load(int maxBooks);

}  // namespace recentBookLoader
