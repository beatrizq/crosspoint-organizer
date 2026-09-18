"""
PlatformIO pre-build script: fix a dead-code bug in NimBLE-Arduino's ESP32
FreeRTOS port that leaves BleNotifyRelay::resume() unable to safely follow a
prior BleNotifyRelay::pause().

esp_nimble_disable() (nimble_port_freertos.c) runs on the NimBLE host task
itself (host_task() -> nimble_port_freertos_deinit() -> esp_nimble_disable()).
vTaskDelete(host_task_h) there is therefore a FreeRTOS self-delete, which does
not return -- the line right after it, `host_task_h = NULL;`, is unreachable
dead code. That leaves the file-static host_task_h stale across a
deinit()/init() round-trip. This patch reorders the two statements so the
handle is nulled before the (non-returning) self-delete.

Not a git checkout (unlike JPEGDEC, pulled from GitHub), so this uses
patch_wolfssl.py's marker/exact-text approach rather than `git apply`.
"""

from pathlib import Path

Import("env")  # noqa: F821 (SCons-injected global)

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821

OLD = """esp_err_t esp_nimble_disable(void)
{
    if (host_task_h) {
        vTaskDelete(host_task_h);
        host_task_h = NULL;
    }
    return ESP_OK;
}"""

NEW = """esp_err_t esp_nimble_disable(void)
{
    /* MEMFIX-PORT: this runs on the host task itself (host_task() ->
       nimble_port_freertos_deinit() -> esp_nimble_disable()), so
       vTaskDelete(host_task_h) below is a FreeRTOS self-delete and does not
       return -- the original `host_task_h = NULL;` after it was dead code,
       leaving host_task_h stale across a deinit()/init() round-trip (see
       BleNotifyRelay::pause()/resume()). Null the handle before the
       self-delete instead. */
    if (host_task_h) {
        TaskHandle_t handle = host_task_h;
        host_task_h = NULL;
        vTaskDelete(handle);
    }
    return ESP_OK;
}"""


def patch_file(path: Path) -> None:
    text = path.read_text()
    if NEW in text:
        return  # already patched
    if OLD not in text:
        raise SystemExit(
            "ERROR: patch_nimble.py -- esp_nimble_disable() in %s does not "
            "match the expected unpatched source. The vendored library "
            "version may have changed; update scripts/patch_nimble.py."
            % path.relative_to(PROJECT_DIR)
        )
    path.write_text(text.replace(OLD, NEW, 1))
    print("Patched NimBLE-Arduino: %s" % path.relative_to(PROJECT_DIR))


for target in PROJECT_DIR.glob(
    ".pio/libdeps/*/NimBLE-Arduino/src/nimble/porting/npl/freertos/src/"
    "nimble_port_freertos.c"
):
    patch_file(target)
