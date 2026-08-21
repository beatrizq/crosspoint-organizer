#include "HomeAppOrder.h"

#include <cstdio>

namespace homeAppOrder {
namespace {

// Built-in order. Read leads because the cover card above it opens the last book
// and this opens everything else about books; the four integrations follow.
constexpr AppInfo APPS[APP_COUNT] = {
    {AppId::Read, StrId::STR_MENU_READ, UIIcon::Book},
    {AppId::Tasks, StrId::STR_ORGANIZER_TAB_TASKS, UIIcon::Tasks},
    {AppId::Calendar, StrId::STR_ORGANIZER_TAB_CALENDAR, UIIcon::Calendar},
    {AppId::Budget, StrId::STR_ORGANIZER_TAB_BUDGET, UIIcon::Budget},
    {AppId::Habits, StrId::STR_HABITS, UIIcon::Habits},
};

}  // namespace

const AppInfo& appAt(const int index) {
  if (index < 0 || index >= APP_COUNT) return APPS[0];
  return APPS[index];
}

void parse(const char* stored, int (&out)[APP_COUNT]) {
  bool placed[APP_COUNT] = {};
  int count = 0;

  if (stored != nullptr) {
    for (const char* c = stored; *c != '\0' && count < APP_COUNT; c++) {
      // Single digits, so no separator to skip and no number to accumulate. An
      // id past the end of the table is an app removed since this was written.
      if (*c < '0' || *c > '9') continue;
      const int index = *c - '0';
      if (index >= APP_COUNT) continue;
      if (placed[index]) continue;  // repeated id; the first wins
      out[count++] = index;
      placed[index] = true;
    }
  }

  // Whatever the string did not name goes on the end in built-in order. This is
  // what makes a new app appear for someone who already had an order stored,
  // rather than vanishing from their home screen.
  for (int i = 0; i < APP_COUNT && count < APP_COUNT; i++) {
    if (!placed[i]) out[count++] = i;
  }
}

void format(const int (&order)[APP_COUNT], char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  size_t written = 0;
  for (int i = 0; i < APP_COUNT && written + 1 < outSize; i++) {
    const int index = order[i];
    if (index < 0 || index >= APP_COUNT) continue;
    out[written++] = static_cast<char>('0' + index);
  }
  out[written] = '\0';
}

}  // namespace homeAppOrder
