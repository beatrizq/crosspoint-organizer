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
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/OrganizerActions.h"
#include "util/OrganizerSync.h"
#include "util/TaskWatchdog.h"

namespace {
// Manual entry is capped well above anything worth tapping through by hand;
// a habit that legitimately needs more than this in one sitting is not what
// this screen is for.
constexpr int MAX_HABIT_LOG_AMOUNT = 50;
constexpr int HABIT_LOG_SMALL_STEP = 1;
constexpr int HABIT_LOG_LARGE_STEP = 5;
}  // namespace

void HabitsActivity::loadCaches() {
  HABITIFY_HABITS.loadFromFile();
  HABITIFY_STORE.loadFromFile();
}

void HabitsActivity::onEnter() {
  OrganizerScreenActivity::onEnter();
  if (selectHabitId.empty()) return;

  const std::string targetId = std::move(selectHabitId);
  selectHabitId.clear();  // one-shot, regardless of whether the id is still found below

  const auto& habits = HABITIFY_HABITS.getHabits();
  int targetCacheIndex = -1;
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == targetId) {
      targetCacheIndex = static_cast<int>(i);
      break;
    }
  }
  // Gone, or hidden by "hide completed" since the pick was made: leave
  // onEnter()'s default selection (row 0) rather than landing on nothing.
  if (targetCacheIndex < 0 || !isVisible(static_cast<size_t>(targetCacheIndex))) return;

  const int rows = rowCount();
  for (int row = 0; row < rows; row++) {
    if (cacheIndexForRow(row) == targetCacheIndex) {
      selectedIndex = row + 1;
      break;
    }
  }
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
  // it; saying "Select" would promise an action that cannot happen.
  return HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].unitSymbol.empty() ? "" : tr(STR_SELECT);
}

void HabitsActivity::onRowConfirm() { showRowOptions(); }

void HabitsActivity::showRowOptions() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;
  // Mirrors rowConfirmLabel()'s own guard: nothing to log, so nothing to offer.
  if (HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].unitSymbol.empty()) return;

  std::vector<std::string> options{tr(STR_HABITIFY_LOG), tr(STR_FOCUS_SESSION)};
  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the
        // press, this screen on the release) -- same dance completeSelectedHabit()
        // does below for the picker it pushes in turn.
        if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        // idx 1 (Focus session) has nothing to do yet.
        if (std::get<OptionPickResult>(result.data).index == 0) completeSelectedHabit();
      });
}

void HabitsActivity::completeSelectedHabit() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (habits[static_cast<size_t>(cacheIndex)].unitSymbol.empty()) return;
  const HabitifyHabit& habit = habits[static_cast<size_t>(cacheIndex)];

  // A number entry rather than a single +1: repeatedly confirming "+1" was a
  // chore for anything past one. Defaults to 1 so a single Confirm press still
  // behaves exactly like the old one-tap flow; Left/Right and the side buttons
  // move it further before that press. Select is the same button that syncs
  // from the tab bar one row up, so a misplaced press should not silently log
  // anything - it opens here on Back.
  startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                             renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                             MAX_HABIT_LOG_AMOUNT, HABIT_LOG_SMALL_STEP, HABIT_LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                             /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true, StrId::STR_NONE_OPT,
                             habit.name, habit.unitSymbol),
                         [this, cacheIndex](const ActivityResult& result) {
                           // Confirm may still be physically down (the picker answers on the
                           // press, this screen on the release). Back is swallowed whenever the
                           // result was cancelled at all, since dismissing the picker with Back
                           // is release-triggered here - by then the button is no longer down,
                           // but the release is still what this screen would see next.
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) {
                             LOG_DBG("HABITS", "Log cancelled");
                             return;
                           }
                           const auto amount = std::get<IntervalResult>(result.data).value;
                           performIncrement(cacheIndex, static_cast<float>(amount));
                         });
}

void HabitsActivity::performIncrement(const int cacheIndex, const float amount) {
  // Re-checked: the picker sat on top of this screen for as long as the user took
  // to answer, and an index is not a habit.
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= HABITIFY_HABITS.getHabits().size()) return;
  if (amount <= 0.0f) return;

  {
    // The render task reads the habit list; hold the lock across the change so it
    // never paints a half-updated row.
    RenderLock lock(*this);
    organizerActions::logHabit(static_cast<size_t>(cacheIndex), amount);
  }
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
