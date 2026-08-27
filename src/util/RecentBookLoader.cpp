#include "RecentBookLoader.h"

#include <RecentBooksStore.h>

#include <algorithm>

namespace recentBookLoader {

std::vector<RecentBook> load(const int maxBooks) {
  std::vector<RecentBook> result;
  const auto& books = RECENT_BOOKS.getBooks();
  result.reserve(std::min(static_cast<int>(books.size()), std::max(0, maxBooks)));

  for (const RecentBook& book : books) {
    if (static_cast<int>(result.size()) >= maxBooks) break;
    if (RecentBooksStore::isMissing(book)) continue;
    result.push_back(book);
  }
  return result;
}

}  // namespace recentBookLoader
