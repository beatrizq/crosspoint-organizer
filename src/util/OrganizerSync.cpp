#include "OrganizerSync.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalClient.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <HabitifyClient.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistClient.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <YnabCategoryCache.h>
#include <YnabClient.h>
#include <YnabStore.h>
#include <esp_sntp.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "util/HomeAppOrder.h"
#include "util/TaskWatchdog.h"

namespace organizerSync {
namespace {

// SNTP poll: 100ms x 50 = 5s, matching HalClock::syncFromNTP().
constexpr int NTP_POLL_ATTEMPTS = 50;

// Later of two "YYYY-MM-DD" dates. Today only ever moves forward, so picking the
// newest of the available sources is what keeps a partial one from regressing.
// ISO dates order correctly as plain strings, and "" loses to any real date.
const std::string& laterDate(const std::string& a, const std::string& b) { return b > a ? b : a; }

/**
 * Today from NTP, in the device's configured timezone.
 *
 * Most boards (X3/X4 included) have no RTC. NTP is the only source that knows
 * SETTINGS.clockUtcOffsetQ, so it is the only one that yields the device's own
 * local date; the response Date header is the fallback, and is GMT.
 */
bool resolveTodayDate(std::string& outDate) {
  outDate.clear();
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < NTP_POLL_ATTEMPTS && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(100);
    resetTaskWatchdogIfSubscribed();
  }
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    // Not an error on its own; the response header usually supplies the date.
    LOG_DBG("OSYNC", "NTP sync timed out; relying on the response date");
    return false;
  }

  // Boards that do have an RTC get it set from the same SNTP result.
  if (halClock.isAvailable()) halClock.syncFromNTP();

  uint8_t offsetQ = SETTINGS.clockUtcOffsetQ;
  if (offsetQ > 104) offsetQ = 104;  // clamp a corrupt persisted value to UTC+14
  const time_t local = time(nullptr) + (static_cast<int>(offsetQ) - 48) * 15 * 60;
  struct tm timeinfo;
  gmtime_r(&local, &timeinfo);

  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  outDate = buf;
  return true;
}

// -- per-service error text -------------------------------------------------

const char* taskErrorText(const TodoistClient::Error error) {
  switch (error) {
    case TodoistClient::NO_TOKEN:
      return tr(STR_TODOIST_NO_TOKEN);
    case TodoistClient::AUTH_FAILED:
      return tr(STR_TODOIST_INVALID_TOKEN);
    case TodoistClient::SERVER_ERROR:
      return tr(STR_TODOIST_SERVER_ERROR);
    case TodoistClient::PARSE_ERROR:
      return tr(STR_TODOIST_BAD_RESPONSE);
    case TodoistClient::INVALID_FILTER:
      return tr(STR_TODOIST_INVALID_FILTER);
    case TodoistClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

const char* calendarErrorText(const GCalClient::Error error) {
  switch (error) {
    case GCalClient::NO_TOKEN:
      return tr(STR_GCAL_NOT_LINKED);
    case GCalClient::AUTH_FAILED:
      return tr(STR_GCAL_RELINK_NEEDED);
    case GCalClient::SERVER_ERROR:
      return tr(STR_GCAL_SERVER_ERROR);
    case GCalClient::PARSE_ERROR:
      return tr(STR_GCAL_BAD_RESPONSE);
    case GCalClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

const char* budgetErrorText(const YnabClient::Error error) {
  switch (error) {
    case YnabClient::NO_TOKEN:
    case YnabClient::NO_BUDGET:
      return tr(STR_YNAB_NOT_CONFIGURED);
    case YnabClient::AUTH_FAILED:
      return tr(STR_YNAB_INVALID_TOKEN);
    case YnabClient::NOT_FOUND:
      return tr(STR_YNAB_BUDGET_NOT_FOUND);
    case YnabClient::RATE_LIMITED:
      return tr(STR_YNAB_RATE_LIMITED);
    case YnabClient::SERVER_ERROR:
      return tr(STR_YNAB_SERVER_ERROR);
    case YnabClient::PARSE_ERROR:
      return tr(STR_YNAB_BAD_RESPONSE);
    case YnabClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

const char* habitErrorText(const HabitifyClient::Error error) {
  switch (error) {
    case HabitifyClient::NO_KEY:
      return tr(STR_HABITIFY_NO_KEY);
    case HabitifyClient::AUTH_FAILED:
      return tr(STR_HABITIFY_KEY_REJECTED);
    case HabitifyClient::NOT_FOUND:
      return tr(STR_HABITIFY_HABIT_NOT_FOUND);
    case HabitifyClient::RATE_LIMITED:
      return tr(STR_HABITIFY_RATE_LIMITED);
    case HabitifyClient::SERVER_ERROR:
      return tr(STR_HABITIFY_SERVER_ERROR);
    case HabitifyClient::PARSE_ERROR:
      return tr(STR_HABITIFY_BAD_RESPONSE);
    case HabitifyClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

// -- per-service sync -------------------------------------------------------

const char* runTasks() {
  // NTP first, only because it needs the radio while it is still up.
  std::string ntpDate;
  if (!resolveTodayDate(ntpDate)) ntpDate.clear();

  // Push queued completions before fetching, so the fetched list already
  // reflects them. A copy: clearPending() mutates the queue as we go.
  const std::vector<std::string> pending = TODOIST_TASKS.getPendingIds();
  TodoistClient::Error error = TodoistClient::OK;
  for (const auto& id : pending) {
    // Each push is a full TLS request; the sync runs on the main task.
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::closeTask(id);
    if (error != TodoistClient::OK) {
      LOG_ERR("OSYNC", "Task push failed for %s: %s", id.c_str(), TodoistClient::errorString(error));
      break;  // keep the rest queued for the next attempt
    }
    TODOIST_TASKS.clearPending(id);
  }

  std::vector<TodoistTask> fetched;
  std::string serverDate;
  if (error == TodoistClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::fetchTasks(fetched, serverDate);
    resetTaskWatchdogIfSubscribed();
  }

  // NTP wins when it worked: it is the only source that applies the configured
  // UTC offset. The Date header is the fallback - it cannot be blocked the way
  // NTP can, but it is GMT, so it can read a day off either side of midnight.
  // The previous sync then keeps the date from going backwards.
  std::string today = ntpDate.empty() ? serverDate : ntpDate;
  today = laterDate(today, TODOIST_TASKS.getSyncDate());
  if (today.empty()) {
    LOG_ERR("OSYNC", "Today unresolved: no NTP, no Date header, no previous sync");
  }

  if (error == TodoistClient::OK) {
    RenderLock lock;
    TODOIST_TASKS.setTasks(std::move(fetched), today);
  }
  // Persists the fetched list and whatever the queue push managed to clear, so a
  // failed fetch after a successful push is still recorded.
  TODOIST_TASKS.saveToFile();
  return error == TodoistClient::OK ? nullptr : taskErrorText(error);
}

const char* runCalendar() {
  // The refresh serves two purposes: it mints the access token every request
  // needs, and its HTTP Date header is the device's clock. Most boards have no
  // RTC and SNTP can be blocked on a given network, so anchoring the window on a
  // header that cannot fail when the request succeeded is what makes "today and
  // the next 30 days" mean anything.
  std::string accessToken;
  uint16_t today = civil::NO_DATE;
  const GCalAuth::Error authError = GCalAuth::refreshAccessToken(accessToken, today);

  GCalClient::Error error = GCalClient::OK;
  std::vector<GCalEvent> fetched;

  if (authError != GCalAuth::OK) {
    LOG_ERR("OSYNC", "Token refresh failed: %s", GCalAuth::errorString(authError));
  } else if (today == civil::NO_DATE) {
    // Without a date there is no window to ask for, and guessing would silently
    // show the wrong month.
    LOG_ERR("OSYNC", "No Date header on the token response; cannot anchor the window");
    error = GCalClient::PARSE_ERROR;
  } else {
    const uint16_t lastDay = static_cast<uint16_t>(today + GCAL_WINDOW_DAYS - 1);
    fetched.reserve(GCAL_MAX_EVENTS);
    for (const auto& calendarId : GCAL_STORE.getSelectedCalendars()) {
      resetTaskWatchdogIfSubscribed();
      error = GCalClient::fetchEvents(accessToken, calendarId, today, lastDay, fetched);
      resetTaskWatchdogIfSubscribed();
      if (error != GCalClient::OK) {
        LOG_ERR("OSYNC", "Event fetch failed for %s: %s", calendarId.c_str(), GCalClient::errorString(error));
        break;
      }
      if (fetched.size() >= GCAL_MAX_EVENTS) {
        LOG_INF("OSYNC", "Event cap (%zu) reached; later calendars not fetched", GCAL_MAX_EVENTS);
        break;
      }
    }
  }

  const char* failure = nullptr;
  if (authError != GCalAuth::OK) {
    failure = authError == GCalAuth::INVALID_GRANT ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
  } else if (error != GCalClient::OK) {
    failure = calendarErrorText(error);
  } else {
    RenderLock lock;
    GCAL_EVENTS.setEvents(std::move(fetched), today);
  }
  GCAL_EVENTS.saveToFile();
  return failure;
}

const char* runBudget() {
  // One request, and it carries its own month: the balances are month-scoped and
  // YNAB says which month it answered for, so nothing here needs a clock. That
  // matters on boards with no RTC, and it keeps this to a single call against a
  // token allowed 200 requests an hour.
  //
  // The account tabs are deliberately not synced here. Each is another request
  // against that same hourly allowance, and they are per-tab by design; a
  // sync-everything that quietly spent five of them would be the wrong trade.
  std::vector<YnabCategory> fetched;
  uint16_t month = civil::NO_DATE;
  resetTaskWatchdogIfSubscribed();
  const YnabClient::Error error = YnabClient::fetchSelectedCategories(fetched, month);
  resetTaskWatchdogIfSubscribed();
  if (error != YnabClient::OK) {
    LOG_ERR("OSYNC", "Plan fetch failed: %s", YnabClient::errorString(error));
  }

  if (error == YnabClient::OK) {
    RenderLock lock;
    YNAB_CATEGORIES.setCategories(std::move(fetched), month);
  }
  YNAB_CATEGORIES.saveToFile();
  return error == YnabClient::OK ? nullptr : budgetErrorText(error);
}

const char* runHabits() {
  // Push what is owed before fetching, so the journal that comes back already
  // reflects it. A copy of the ids and amounts: clearPending() mutates the cache
  // as we go, and the fetch replaces the list wholesale.
  struct Owed {
    std::string id;
    std::string unit;
    float amount;
  };
  std::vector<Owed> owed;
  // Reserved against the habit count rather than the number owing: one growth
  // event is three heap operations, and this runs with a TLS session about to be
  // opened, where DRAM fragmentation is what breaks the handshake.
  owed.reserve(HABITIFY_HABITS.getHabits().size());
  for (const auto& habit : HABITIFY_HABITS.getHabits()) {
    if (habit.hasPending() && !habit.unitSymbol.empty()) {
      owed.push_back(Owed{habit.id, habit.unitSymbol, habit.pending});
    }
  }

  HabitifyClient::Error error = HabitifyClient::OK;
  for (const auto& entry : owed) {
    resetTaskWatchdogIfSubscribed();
    error = HabitifyClient::addLog(entry.id, entry.unit, entry.amount);
    if (error != HabitifyClient::OK) {
      LOG_ERR("OSYNC", "Habit push failed for %s: %s", entry.id.c_str(), HabitifyClient::errorString(error));
      break;  // keep the rest owed for the next attempt
    }
    HABITIFY_HABITS.clearPending(entry.id, entry.amount);
  }

  std::vector<HabitifyHabit> fetched;
  uint16_t date = civil::NO_DATE;
  if (error == HabitifyClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = HabitifyClient::fetchJournal(fetched, date);
    resetTaskWatchdogIfSubscribed();
  }

  if (error == HabitifyClient::OK) {
    RenderLock lock;
    // Carries over anything still owed - a press that landed between the push
    // above and this fetch.
    HABITIFY_HABITS.setHabits(std::move(fetched), date);
  }
  HABITIFY_HABITS.saveToFile();
  return error == HabitifyClient::OK ? nullptr : habitErrorText(error);
}

}  // namespace

const char* name(const Service service) {
  // The same name the home grid and the app's own screen use, nickname included:
  // a sync list that called an app something else would read as a different app.
  switch (service) {
    case Service::Tasks:
      return homeAppOrder::displayName(homeAppOrder::AppId::Tasks);
    case Service::Calendar:
      return homeAppOrder::displayName(homeAppOrder::AppId::Calendar);
    case Service::Budget:
      return homeAppOrder::displayName(homeAppOrder::AppId::Budget);
    case Service::Habits:
      return homeAppOrder::displayName(homeAppOrder::AppId::Habits);
  }
  return "";
}

bool isConfigured(const Service service) {
  switch (service) {
    case Service::Tasks:
      return TODOIST_STORE.hasToken();
    case Service::Calendar:
      // Linked and told which calendars to read: without a selection the sync has
      // nothing to ask for.
      return GCAL_STORE.hasClientCredentials() && GCAL_STORE.isLinked() && !GCAL_STORE.getSelectedCalendars().empty();
    case Service::Budget:
      return YNAB_STORE.isConfigured() && !YNAB_STORE.getSelectedCategories().empty();
    case Service::Habits:
      return HABITIFY_STORE.hasApiKey();
  }
  return false;
}

const char* run(const Service service) {
  switch (service) {
    case Service::Tasks:
      return runTasks();
    case Service::Calendar:
      return runCalendar();
    case Service::Budget:
      return runBudget();
    case Service::Habits:
      return runHabits();
  }
  return nullptr;
}

}  // namespace organizerSync
