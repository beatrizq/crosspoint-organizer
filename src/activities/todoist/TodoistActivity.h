#pragma once
#include <TodoistClient.h>

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Today screen: the tasks Todoist reports as due today or overdue, rendered
 * from the SD cache so the screen opens with the radio off.
 *
 * Select completes the selected task (queued locally, pushed on the next sync);
 * holding Select brings Wi-Fi up, pushes the queue, and re-fetches the list.
 */
class TodoistActivity final : public Activity {
 public:
  explicit TodoistActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    LIST,     // Showing the cached list
    SYNCING,  // Blocking network work in progress
    FAILED,   // Last sync failed; statusMessage holds the reason
  };

  // Rows come straight from the cache, so the count follows it.
  int taskCount() const;

  void completeSelected();
  void startSync();
  void performSync();
  // Pull the date over NTP (no RTC on most boards) into "YYYY-MM-DD" local
  // time. Falls back to the cached sync date so overdue flagging still works.
  bool resolveTodayDate(std::string& outDate) const;
  // Snapshot the rendered list to /sleep.bmp so the sleeping device shows it.
  void saveSleepWallpaper() const;
  static const char* errorText(TodoistClient::Error error);

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  State state = State::LIST;
  const char* statusMessage = nullptr;  // Translated; only read in FAILED state
  bool wifiActivated = false;
};
