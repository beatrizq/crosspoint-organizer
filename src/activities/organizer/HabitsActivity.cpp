#include "HabitsActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "OrganizerLabels.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/OrganizerSync.h"
#include "util/TaskWatchdog.h"

void HabitsActivity::loadCaches() {
  HABITIFY_HABITS.loadFromFile();
  HABITIFY_STORE.loadFromFile();
}

const char* HabitsActivity::screenTitle() const { return homeAppOrder::displayName(homeAppOrder::AppId::Habits); }

const char* HabitsActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::TODAY:
      return tr(STR_TODAY);
  }
  return "";
}

// -- rows -------------------------------------------------------------------

bool HabitsActivity::isVisible(const size_t cacheIndex) const {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return false;
  // The setting turns the list into what is left to do rather than a checklist of
  // what is done. A habit with no goal can never read as complete, so it stays.
  if (HABITIFY_STORE.getHideCompleted() && habits[cacheIndex].isComplete()) return false;
  return true;
}

int HabitsActivity::rowCount() const {
  const auto& habits = HABITIFY_HABITS.getHabits();
  int count = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (isVisible(i)) count++;
  }
  return count;
}

int HabitsActivity::cacheIndexForRow(const int row) const {
  if (row < 0) return -1;
  const auto& habits = HABITIFY_HABITS.getHabits();
  int seen = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (!isVisible(i)) continue;
    if (seen == row) return static_cast<int>(i);
    seen++;
  }
  return -1;
}

void HabitsActivity::formatProgress(const HabitifyHabit& habit, char* out, const size_t outSize) const {
  if (out == nullptr || outSize == 0) return;
  // %g rather than %f: a count-based habit reads "1/3", not "1.000000/3.000000",
  // and a distance habit still shows its fraction as "2.5/5".
  if (!habit.hasTarget()) {
    // No goal means no denominator to show; the accumulated figure is all there
    // is to say about it.
    snprintf(out, outSize, "%g", static_cast<double>(habit.shownCurrent()));
    return;
  }
  snprintf(out, outSize, "%g/%g", static_cast<double>(habit.shownCurrent()), static_cast<double>(habit.target));
}

void HabitsActivity::drawRow(const RowLayout& layout) const {
  const int cacheIndex = cacheIndexForRow(layout.index);
  if (cacheIndex < 0) return;
  const HabitifyHabit& habit = HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)];

  char progress[24];
  formatProgress(habit, progress, sizeof(progress));

  // Same layout as a Budget category: the figure hard right, drawn whole because
  // it is the part being glanced at, and the name takes what is left. Two spaces
  // of the row's own font between them, so the gap tracks the font-size setting
  // and a truncated name cannot run into the number.
  const int gap = renderer.getSpaceWidth(layout.titleFont) * 2;
  // A met goal is set in bold, so a finished habit is distinguishable at a glance
  // without needing a tick glyph the themes do not all provide.
  const auto style = habit.isComplete() ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int progressWidth = renderer.getTextWidth(layout.titleFont, progress, style);
  const int nameWidth = std::max(0, layout.width - progressWidth - gap);

  const auto shownName = renderer.truncatedText(layout.titleFont, habit.name.c_str(), nameWidth, style);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownName.c_str(), layout.ink, style);
  renderer.drawText(layout.titleFont, layout.x + layout.width - progressWidth, layout.textY, progress, layout.ink,
                    style);
}

void HabitsActivity::formatStatus(char* out, const size_t outSize) const {
  if (!HABITIFY_HABITS.hasSynced()) {
    snprintf(out, outSize, "%s", tr(STR_HABITIFY_NEVER_SYNCED));
    return;
  }

  // The day, because progress is per-day: a cache from yesterday shows
  // yesterday's counts, and the date is the only thing that says so.
  char date[16];
  organizer::formatDayLabel(HABITIFY_HABITS.getSyncDate(), date, sizeof(date));

  // How many habits have met their goal, which is the one number worth carrying
  // in the header.
  const auto& habits = HABITIFY_HABITS.getHabits();
  const int done = static_cast<int>(
      std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.isComplete(); }));

  if (HABITIFY_HABITS.hasPending()) {
    char waiting[32];
    snprintf(waiting, sizeof(waiting), tr(STR_HABITIFY_PENDING_LOGS), static_cast<int>(HABITIFY_HABITS.pendingCount()));
    snprintf(out, outSize, "%s %s | %d/%d", date, waiting, done, static_cast<int>(habits.size()));
    return;
  }
  snprintf(out, outSize, "%s  ·  %d/%d", date, done, static_cast<int>(habits.size()));
}

const char* HabitsActivity::emptyMessage() const {
  if (!HABITIFY_HABITS.hasSynced()) return tr(STR_HABITIFY_NEVER_SYNCED);
  // Everything hidden is a different state from having no habits, and it is the
  // one the user can act on - by turning the setting off.
  if (HABITIFY_STORE.getHideCompleted() && !HABITIFY_HABITS.getHabits().empty()) {
    return tr(STR_HABITIFY_ALL_DONE);
  }
  return tr(STR_HABITIFY_NO_HABITS);
}

const char* HabitsActivity::syncingMessage() const { return tr(STR_HABITIFY_SYNCING); }

// -- completion -------------------------------------------------------------

const char* HabitsActivity::rowConfirmLabel() const {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return "";
  // A habit with no goal has no unit either, so there is nothing to log against
  // it; saying "Complete" would promise an action that cannot happen.
  return HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].unitSymbol.empty() ? ""
                                                                                         : tr(STR_HABITIFY_COMPLETE);
}

void HabitsActivity::onRowConfirm() { completeSelectedHabit(); }

void HabitsActivity::completeSelectedHabit() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (habits[static_cast<size_t>(cacheIndex)].unitSymbol.empty()) return;

  // Asked rather than done, matching the Todoist screen: Select is the same
  // button that syncs from the tab bar one row up, so a misplaced press should not
  // silently move a number. The prompt names the habit, because the list is behind
  // it by then, and it opens on Cancel.
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_HABITIFY_COMPLETE_PROMPT),
                                                                habits[static_cast<size_t>(cacheIndex)].name),
                         [this, cacheIndex](const ActivityResult& result) {
                           // The popup answered on the press; this screen acts on the release, and
                           // the button may still be down.
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled) {
                             LOG_DBG("HABITS", "Increment cancelled");
                             return;
                           }
                           performIncrement(cacheIndex);
                         });
}

void HabitsActivity::performIncrement(const int cacheIndex) {
  // Re-checked: the prompt sat on top of this screen for as long as the user took
  // to answer, and an index is not a habit.
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= HABITIFY_HABITS.getHabits().size()) return;

  {
    // The render task reads the habit list; hold the lock across the change so it
    // never paints a half-updated row.
    RenderLock lock(*this);
    const auto& habits = HABITIFY_HABITS.getHabits();
    if (habits[static_cast<size_t>(cacheIndex)].unitSymbol.empty()) return;
    LOG_DBG("HABITS", "+%g to %s", static_cast<double>(HABITIFY_INCREMENT),
            habits[static_cast<size_t>(cacheIndex)].name.c_str());
    HABITIFY_HABITS.addPending(static_cast<size_t>(cacheIndex), HABITIFY_INCREMENT);
  }
  HABITIFY_HABITS.saveToFile();
  requestUpdate(true);
  // A press with the radio off moves the number on screen, which is as much a
  // change as a sync is.
  updateSleepScreen();
}

// -- sync -------------------------------------------------------------------

void HabitsActivity::startSync() {
  if (!HABITIFY_STORE.hasApiKey()) {
    failSync(tr(STR_HABITIFY_NO_KEY));
    return;
  }
  runSync([this] { performSync(); });
}

void HabitsActivity::performSync() {
  // The push-then-fetch sequence lives in organizerSync so the home screen's
  // sync-everything can drive it over one Wi-Fi association.
  const char* failure = organizerSync::run(organizerSync::Service::Habits);

  // Drop the radio before repainting; the full teardown happens on the silent
  // reboot in onExit().
  tearDownRadio();
  finishSync(failure);
  if (failure == nullptr) updateSleepScreen();
}
