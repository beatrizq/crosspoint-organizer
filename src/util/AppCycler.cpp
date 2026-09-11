#include "AppCycler.h"

#include <algorithm>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#ifdef ENABLE_BLE_NOTIFY_SPIKE
#include "network/BleNotificationsActivity.h"
#endif

namespace appCycler {
namespace {

// Same app list and gating HomeActivity::buildEntries() uses for the grid,
// so cycling lands on exactly the apps (and order) the user actually sees.
std::vector<homeAppOrder::AppId> buildAppList() {
  std::vector<homeAppOrder::AppId> apps;
  apps.reserve(homeAppOrder::APP_COUNT);
  int order[homeAppOrder::APP_COUNT];
  homeAppOrder::parse(SETTINGS.homeAppOrder, order);
  for (const int index : order) {
    const auto& app = homeAppOrder::appAt(index);
#ifndef ENABLE_BLE_NOTIFY_SPIKE
    if (app.id == homeAppOrder::AppId::Notifications) continue;
#endif
    apps.push_back(app.id);
  }
  return apps;
}

}  // namespace

homeAppOrder::AppId nextApp(const homeAppOrder::AppId current) {
  const auto apps = buildAppList();
  if (apps.empty()) return current;
  const auto it = std::find(apps.begin(), apps.end(), current);
  const size_t idx = it == apps.end() ? 0 : static_cast<size_t>(it - apps.begin());
  return apps[(idx + 1) % apps.size()];
}

homeAppOrder::AppId previousApp(const homeAppOrder::AppId current) {
  const auto apps = buildAppList();
  if (apps.empty()) return current;
  const auto it = std::find(apps.begin(), apps.end(), current);
  const size_t idx = it == apps.end() ? 0 : static_cast<size_t>(it - apps.begin());
  return apps[(idx + apps.size() - 1) % apps.size()];
}

void open(const homeAppOrder::AppId app) {
  switch (app) {
    case homeAppOrder::AppId::Read:
      activityManager.goToReadMenu();
      break;
    case homeAppOrder::AppId::Tasks:
      activityManager.goToTasks();
      break;
    case homeAppOrder::AppId::Calendar:
      activityManager.goToCalendar();
      break;
    case homeAppOrder::AppId::Budget:
      activityManager.goToBudget();
      break;
    case homeAppOrder::AppId::Habits:
      activityManager.goToHabits();
      break;
#ifdef ENABLE_BLE_NOTIFY_SPIKE
    case homeAppOrder::AppId::Notifications:
      activityManager.goToBleNotifications();
      break;
#endif
    default:
      break;
  }
}

}  // namespace appCycler
