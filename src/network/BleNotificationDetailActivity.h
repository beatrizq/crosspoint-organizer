#pragma once

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <string>

#include "activities/Activity.h"

// Full, untruncated view of one BleNotificationQueue entry -- reached from
// BleNotificationsActivity's Select (Confirm). Fields are copied in rather
// than referenced by index: the queue can be dismissed from the list this
// pushed on top of, and a copy means that can never leave this screen holding
// a dangling read.
class BleNotificationDetailActivity final : public Activity {
  std::string sender;
  std::string title;
  std::string content;
  bool isCall;
  uint8_t hour;
  uint8_t minute;

 public:
  BleNotificationDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string sender,
                                std::string title, std::string content, const bool isCall, const uint8_t hour,
                                const uint8_t minute)
      : Activity("BleNotificationDetail", renderer, mappedInput),
        sender(std::move(sender)),
        title(std::move(title)),
        content(std::move(content)),
        isCall(isCall),
        hour(hour),
        minute(minute) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#endif  // ENABLE_BLE_NOTIFY_SPIKE
