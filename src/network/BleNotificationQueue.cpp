#include "BleNotificationQueue.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <algorithm>
#include <cstring>

void BleNotificationQueue::push(const uint32_t id, const bool isCall, const char* sender, const char* title,
                                const char* content, const uint8_t hour, const uint8_t minute) {
  BleNotificationEntry& e = entries[pos];
  e.id = id;
  e.isCall = isCall;
  strlcpy(e.sender, sender != nullptr ? sender : "", sizeof(e.sender));
  strlcpy(e.title, title != nullptr ? title : "", sizeof(e.title));
  strlcpy(e.content, content != nullptr ? content : "", sizeof(e.content));
  e.hour = hour;
  e.minute = minute;

  pos = static_cast<uint8_t>((pos + 1) % CAPACITY);
  if (fill < CAPACITY) fill++;
  if (unreadCount < CAPACITY) unreadCount++;
}

const BleNotificationEntry& BleNotificationQueue::getEntry(const size_t indexFromNewest) const {
  const uint8_t slot = static_cast<uint8_t>((pos + CAPACITY - 1 - indexFromNewest) % CAPACITY);
  return entries[slot];
}

void BleNotificationQueue::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (uint8_t i = 0; i < CAPACITY; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = entries[i].id;
    o["isCall"] = entries[i].isCall;
    o["sender"] = entries[i].sender;
    o["title"] = entries[i].title;
    o["content"] = entries[i].content;
    o["hour"] = entries[i].hour;
    o["minute"] = entries[i].minute;
  }
  doc["pos"] = pos;
  doc["fill"] = fill;
  doc["unreadCount"] = unreadCount;
}

bool BleNotificationQueue::fromJson(JsonVariantConst doc) {
  JsonArrayConst arr = doc["entries"];
  const size_t actualCount =
      arr.isNull() ? 0 : std::min(static_cast<size_t>(arr.size()), static_cast<size_t>(CAPACITY));
  for (size_t i = 0; i < actualCount; i++) {
    JsonObjectConst o = arr[i];
    entries[i].id = o["id"] | static_cast<uint32_t>(0);
    entries[i].isCall = o["isCall"] | false;
    strlcpy(entries[i].sender, o["sender"] | "", sizeof(entries[i].sender));
    strlcpy(entries[i].title, o["title"] | "", sizeof(entries[i].title));
    strlcpy(entries[i].content, o["content"] | "", sizeof(entries[i].content));
    entries[i].hour = o["hour"] | static_cast<uint8_t>(0);
    entries[i].minute = o["minute"] | static_cast<uint8_t>(0);
  }
  for (size_t i = actualCount; i < CAPACITY; i++) entries[i] = BleNotificationEntry{};

  pos = doc["pos"] | static_cast<uint8_t>(0);
  if (pos >= CAPACITY) pos = 0;
  fill = doc["fill"] | static_cast<uint8_t>(0);
  fill = static_cast<uint8_t>(std::min(static_cast<int>(fill), static_cast<int>(actualCount)));
  unreadCount = doc["unreadCount"] | static_cast<uint8_t>(0);
  unreadCount = std::min(unreadCount, fill);
  return true;
}

#endif  // ENABLE_BLE_NOTIFY_SPIKE
