#include "HabitsActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <I18n.h>
#include <Logging.h>

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
  rebuildTabs();
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

  // All (always visibleAreaIds[0]) matches everything, so it is only used as
  // the fallback -- a more specific tab, when the habit's area still has one,
  // is where it actually belongs.
  int targetTab = 0;
  for (size_t t = 1; t < visibleAreaIds.size(); t++) {
    if (matchesArea(visibleAreaIds[t], static_cast<size_t>(targetCacheIndex))) {
      targetTab = static_cast<int>(t);
      break;
    }
  }
  setTab(targetTab);

  const int rows = rowCount();
  for (int row = 0; row < rows; row++) {
    if (cacheIndexForRow(row) == targetCacheIndex) {
      selectedIndex = row + 1;
      break;
    }
  }
}

const char* HabitsActivity::screenTitle() const { return homeAppOrder::displayName(homeAppOrder::AppId::Habits); }

const std::string& HabitsActivity::areaIdAt(const int index) const {
  static const std::string kAll;
  if (index < 0 || static_cast<size_t>(index) >= visibleAreaIds.size()) return kAll;
  return visibleAreaIds[static_cast<size_t>(index)];
}

const char* HabitsActivity::tabLabel(const int index) const {
  const std::string& areaId = areaIdAt(index);
  if (areaId.empty()) return tr(STR_HABITS_TAB_ALL);
  return HABITIFY_HABITS.getAreaName(areaId);
}

void HabitsActivity::rebuildTabs() {
  // The area selected now, so the same tab stays under the user across a
  // rebuild even though its index may move when a tab ahead of it appears or
  // goes (an area renamed or deleted in Habitify itself, say).
  const std::string wanted = currentAreaId();

  visibleAreaIds.clear();
  visibleAreaIds.reserve(HABITIFY_HABITS.getAreas().size() + 1);
  // All always shows: it is every habit regardless of area, and the one tab a
  // successful sync cannot leave empty. The rest earn their place by having a
  // habit, same as TasksActivity's own date tabs.
  visibleAreaIds.push_back("");
  for (const auto& area : HABITIFY_HABITS.getAreas()) {
    if (countForArea(area.id) > 0) visibleAreaIds.push_back(area.id);
  }

  int restored = 0;
  for (size_t i = 0; i < visibleAreaIds.size(); i++) {
    if (visibleAreaIds[i] == wanted) {
      restored = static_cast<int>(i);
      break;
    }
  }
  // Falls back to All when the selected area just emptied or was removed.
  setTab(restored);

  // The new tab's list can be shorter than the old one, so the row selection
  // has to be pulled back inside it. Index 0 is the tab bar, always valid.
  const int rows = rowCount();
  if (selectedRow() >= rows) selectedIndex = rows;
  if (selectedIndex < 0) selectedIndex = 0;
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

bool HabitsActivity::matchesArea(const std::string& areaId, const size_t cacheIndex) const {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return false;
  if (areaId.empty()) return true;  // All
  return habits[cacheIndex].areaId == areaId;
}

int HabitsActivity::countForArea(const std::string& areaId) const {
  const auto& habits = HABITIFY_HABITS.getHabits();
  int count = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (matchesArea(areaId, i)) count++;
  }
  return count;
}

int HabitsActivity::rowCount() const {
  const auto& habits = HABITIFY_HABITS.getHabits();
  const std::string& areaId = currentAreaId();
  int count = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (isVisible(i) && matchesArea(areaId, i)) count++;
  }
  return count;
}

int HabitsActivity::cacheIndexForRow(const int row) const {
  if (row < 0) return -1;
  const auto& habits = HABITIFY_HABITS.getHabits();
  const std::string& areaId = currentAreaId();
  int seen = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (!isVisible(i) || !matchesArea(areaId, i)) continue;
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
  // completedByStatus can go true (Complete tapped locally, or Habitify's own
  // status already says done) before current/pending's own arithmetic has
  // caught up to target -- show target/target rather than a fraction that
  // would still read as short right next to the row's own bold "done"
  // styling (see isComplete()). The underlying figures are untouched; this
  // only affects what gets drawn.
  const float shown =
      habit.completedByStatus && habit.shownCurrent() < habit.target ? habit.target : habit.shownCurrent();
  snprintf(out, outSize, "%g/%g", static_cast<double>(shown), static_cast<double>(habit.target));
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
  const EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  const int progressWidth = renderer.getTextWidth(layout.titleFont, progress, style);
  const int nameWidth = std::max(0, layout.width - progressWidth - gap);

  const auto shownName = renderer.truncatedText(layout.titleFont, habit.name.c_str(), nameWidth, style);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownName.c_str(), layout.ink, style);
  renderer.drawText(layout.titleFont, layout.x + layout.width - progressWidth, layout.textY, progress, layout.ink,
                    style);

  // A met goal is greyed, so a finished habit is distinguishable at a glance
  // without needing a tick glyph the themes do not all provide. Dithered
  // rather than a real grey -- the panel is 1-bit -- same technique
  // dimText() already uses for a row's subordinate text elsewhere on these
  // screens; skipped on a selected row (ink false), which is already
  // inverted and would dither wrong.
  if (habit.isComplete()) {
    dimText(layout.x, layout.textY, layout.titleFont, shownName.c_str(), layout.ink);
    dimText(layout.x + layout.width - progressWidth, layout.textY, layout.titleFont, progress, layout.ink);
  }
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

  if (HABITIFY_HABITS.hasPending()) {
    char waiting[32];
    snprintf(waiting, sizeof(waiting), tr(STR_HABITIFY_PENDING_LOGS), static_cast<int>(HABITIFY_HABITS.pendingCount()));
    snprintf(out, outSize, "%s  ·  %s", date, waiting);
    return;
  }
  snprintf(out, outSize, "%s", date);
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
  // Complete needs no unit, so every habit has something to offer here now -
  // a goal-less one just skips straight to Options without a Log entry.
  return tr(STR_SELECT);
}

void HabitsActivity::onRowConfirm() { showRowOptions(); }

void HabitsActivity::showRowOptions() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;
  // A habit with no goal has no unit, so nothing can be logged against it (see
  // logHabit()) - Log is left off the menu rather than shown and silently
  // failing. Complete needs no unit, so it is offered either way.
  const bool canLog = !HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].unitSymbol.empty();
  std::vector<std::string> options;
  if (canLog) options.push_back(tr(STR_HABITIFY_LOG));
  options.push_back(tr(STR_COMPLETE_HABIT));
  options.push_back(tr(STR_FOCUS_SESSION));
  const int logIdx = canLog ? 0 : -1;
  const int completeIdx = canLog ? 1 : 0;
  const int focusIdx = canLog ? 2 : 1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, cacheIndex, logIdx, completeIdx, focusIdx](const ActivityResult& result) {
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
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == logIdx) {
          completeSelectedHabit();
        } else if (idx == completeIdx) {
          markSelectedHabitComplete();
        } else if (idx == focusIdx) {
          offerFocusSession(cacheIndex);
        }
      });
}

void HabitsActivity::offerFocusSession(const int cacheIndex) {
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= HABITIFY_HABITS.getHabits().size()) return;
  const std::string text = HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].name;
  const std::string id = HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].id;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, text, id](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Back)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(text, id, /*isHabit=*/true,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
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

void HabitsActivity::markSelectedHabitComplete() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= HABITIFY_HABITS.getHabits().size()) return;

  {
    // Same reasoning as performIncrement(): the render task reads the habit
    // list, and completing one is as much a change to it as logging is.
    RenderLock lock(*this);
    organizerActions::completeHabit(static_cast<size_t>(cacheIndex));
  }
  requestUpdate(true);
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

  if (failure == nullptr) {
    // A new result set means new area membership, so which tabs exist changes
    // with it -- same reasoning as TasksActivity's own post-sync rebuild.
    RenderLock lock(*this);
    rebuildTabs();
  }
  finishSync(failure);
  if (failure == nullptr) updateSleepScreen();
}
