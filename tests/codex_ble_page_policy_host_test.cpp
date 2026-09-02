#include "codex_ble_page_policy.h"

#include <assert.h>

int main()
{
    assert(codex_ble_page_should_run(
        kWorkPageCodexUsage, false, false, false, false, false));
    assert(!codex_ble_page_should_run(
        kWorkPageWeatherClock, false, false, false, false, false));
    assert(!codex_ble_page_should_run(
        kWorkPageCodexUsage, true, false, false, false, false));
    assert(!codex_ble_page_should_run(
        kWorkPageCodexUsage, false, true, false, false, false));

    // Settings is a temporary overlay over CODEX and keeps the connection.
    assert(codex_ble_page_should_run(
        kWorkPageCodexUsage, false, false, true, true, false));
    // Other auxiliary pages still stop BLE.
    assert(!codex_ble_page_should_run(
        kWorkPageCodexUsage, false, false, true, false, false));
    // Starting an OTA update stops BLE even while settings remains visible.
    assert(!codex_ble_page_should_run(
        kWorkPageCodexUsage, false, false, true, true, true));
    // Disabling/removing CODEX changes the active work page and stops BLE.
    assert(!codex_ble_page_should_run(
        kWorkPageWeatherClock, false, false, true, true, false));
    assert(codex_ble_icon_should_show(kWorkPageCodexUsage, true, true));
    assert(!codex_ble_icon_should_show(kWorkPageWeatherClock, true, true));
    assert(!codex_ble_icon_should_show(kWorkPageCodexUsage, false, true));
    assert(!codex_ble_icon_should_show(kWorkPageCodexUsage, true, false));
    return 0;
}
