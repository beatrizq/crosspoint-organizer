#include "Activity.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "ActivityManager.h"
#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

Activity::ListTouchResult Activity::handleListTouch(int& selectedIndex, const int itemCount, const int listTop,
                                                    const int listHeight, const bool hasSubtitle) {
  int touched = -1;
  if (mappedInput.wasListItemTouchedDown(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    if (selectedIndex != touched) {
      selectedIndex = touched;
      requestUpdate();
    }
    return ListTouchResult::Consumed;
  }
  if (mappedInput.wasListItemTapped(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    selectedIndex = touched;
    return ListTouchResult::Activated;
  }
  return ListTouchResult::None;
}

void Activity::editSettingsText(const char* title, char* field, const size_t fieldSize) {
  if (field == nullptr || fieldSize == 0) return;
  // maxLength is the field minus its terminator; KeyboardEntryActivity counts
  // characters, so the copy below cannot overflow.
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title == nullptr ? "" : title,
                                                                 std::string(field), fieldSize - 1),
                         [this, field, fieldSize](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const std::string& text = std::get<KeyboardResult>(result.data).text;
                           // Guarded, as CrossPointSettings expects of its writers:
                           // an unchanged value must not cost an SD write.
                           if (strncmp(field, text.c_str(), fieldSize) == 0) return;
                           snprintf(field, fieldSize, "%s", text.c_str());
                           SETTINGS.saveToFile();
                           requestUpdate();
                         });
}
