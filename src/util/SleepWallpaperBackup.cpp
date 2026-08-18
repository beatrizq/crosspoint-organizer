#include "SleepWallpaperBackup.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

namespace {

constexpr char SLEEP_PATH[] = "/sleep.bmp";
constexpr char BACKUP_PATH[] = "/.crosspoint/sleep_backup.bmp";
// The restore lands here first, so a copy that fails part-way cannot leave a
// truncated file where the wallpaper should be.
constexpr char RESTORE_TEMP_PATH[] = "/.crosspoint/sleep_restore.tmp";
constexpr char CACHE_DIR[] = "/.crosspoint";

// One SD block-aligned chunk. On the heap rather than the stack: 2KB is an
// order of magnitude past what a task stack here should carry, and it is freed
// the moment the copy ends.
constexpr size_t COPY_CHUNK = 2048;

bool copyFile(const char* from, const char* to) {
  HalFile in;
  if (!Storage.openFileForRead("SWB", from, in)) return false;
  HalFile out;
  if (!Storage.openFileForWrite("SWB", to, out)) return false;

  auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_CHUNK);
  if (!buffer) {
    LOG_ERR("SWB", "OOM: %zu bytes", COPY_CHUNK);
    return false;
  }

  int bytesRead;
  while ((bytesRead = in.read(buffer.get(), COPY_CHUNK)) > 0) {
    if (out.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
      LOG_ERR("SWB", "Short write copying %s to %s", from, to);
      return false;
    }
  }
  out.flush();
  // A negative count is a read error; 0 is a clean end of file.
  if (bytesRead < 0) {
    LOG_ERR("SWB", "Read error copying %s", from);
    return false;
  }
  return true;
}

}  // namespace

bool SleepWallpaperBackup::hasBackup() { return Storage.exists(BACKUP_PATH); }

bool SleepWallpaperBackup::captureIfAbsent() {
  // A copy already held is the original one; the file on the card by now is
  // whatever last overwrote it, which is exactly what must not be preserved.
  if (hasBackup()) return false;
  if (!Storage.exists(SLEEP_PATH)) return false;

  Storage.ensureDirectoryExists(CACHE_DIR);
  if (!copyFile(SLEEP_PATH, BACKUP_PATH)) {
    // A half-written copy would restore a corrupt wallpaper later.
    Storage.remove(BACKUP_PATH);
    return false;
  }
  LOG_INF("SWB", "Sleep screen wallpaper preserved");
  return true;
}

bool SleepWallpaperBackup::restore() {
  if (!hasBackup()) return false;

  if (!copyFile(BACKUP_PATH, RESTORE_TEMP_PATH)) {
    Storage.remove(RESTORE_TEMP_PATH);
    return false;
  }

  // Only now is the file on the card expendable: everything that could fail
  // has, and the copy is still held until the swap lands.
  Storage.remove(SLEEP_PATH);
  if (!Storage.rename(RESTORE_TEMP_PATH, SLEEP_PATH)) {
    LOG_ERR("SWB", "Failed to move the restored wallpaper into place");
    Storage.remove(RESTORE_TEMP_PATH);
    return false;
  }

  Storage.remove(BACKUP_PATH);
  LOG_INF("SWB", "Sleep screen wallpaper restored");
  return true;
}

void SleepWallpaperBackup::discard() {
  if (!hasBackup()) return;
  Storage.remove(BACKUP_PATH);
  LOG_DBG("SWB", "Preserved wallpaper dropped");
}
