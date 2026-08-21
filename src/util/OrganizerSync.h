#pragma once

#include <cstdint>

/**
 * The network half of each organizer screen's sync, callable on its own.
 *
 * Each of Tasks, Calendar, Budget and Habits used to own its whole sync: the
 * requests, applying the result to its cache, and the radio either side. That
 * left no way to sync more than one at a time, and the home screen's "sync
 * everything" needs exactly that - four services over one Wi-Fi association
 * rather than four.
 *
 * So the request-and-apply part lives here and the radio does not. Bringing the
 * association up and taking it down belongs to the caller, which is what lets a
 * caller syncing all four pay for it once. Nothing here renders, holds screen
 * state, or reports progress; the caller owns all of that.
 *
 * These block for as long as the requests take - tens of seconds over a slow
 * link - and reset the task watchdog as they go, exactly as the per-screen syncs
 * did. Call them from the main task with the radio already up.
 */
namespace organizerSync {

enum class Service : uint8_t {
  Tasks = 0,
  Calendar = 1,
  Budget = 2,
  Habits = 3,
};

constexpr int SERVICE_COUNT = 4;

inline Service serviceAt(const int index) { return static_cast<Service>(index); }

/** The service's name, translated, for a progress row. */
const char* name(Service service);

/**
 * Whether the service has enough set up to be worth a request.
 *
 * A caller syncing everything skips what is not configured rather than
 * reporting it as a failure: an unconfigured integration is not a broken one.
 */
bool isConfigured(Service service);

/**
 * Runs the service's requests and applies the result to its cache, saving it.
 *
 * Returns nullptr on success, or a translated reason on failure. The cache is
 * mutated under a RenderLock, so a paint cannot catch it half-updated.
 *
 * Does not touch the radio, and does not snapshot the sleep screen - that one
 * belongs to the Tasks screen, which is the only caller with the task list in
 * its framebuffer to snapshot.
 */
const char* run(Service service);

}  // namespace organizerSync
