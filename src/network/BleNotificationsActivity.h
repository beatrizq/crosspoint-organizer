#pragma once

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Lists BleNotificationQueue's entries, most recent first (top to bottom --
// matches BleNotificationQueue::getEntry()'s own "0 = newest" convention, so
// row index and queue index line up directly). Reads straight from the
// singleton rather than copying into a local list: the queue is already a
// small, bounded, always-in-RAM structure, so there is nothing to load.
class BleNotificationsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  // Set when a long-press has fired; input is swallowed until Confirm is
  // released again so the release doesn't also open the detail view Select
  // opens on a plain press -- same guard RecentBooksActivity uses for its own
  // long-press action.
  bool longPressFired = false;

  void openDetail();
  void dismissAll();

 public:
  explicit BleNotificationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleNotifications", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#endif  // ENABLE_BLE_NOTIFY_SPIKE
