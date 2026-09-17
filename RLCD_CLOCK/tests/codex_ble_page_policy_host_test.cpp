#include "codex_ble_page_policy.h"

#include <assert.h>

int main()
{
    const CodexBlePageDecision normal = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        false,
        false,
        kOtaIdle);
    assert(normal.should_run);
    assert(!normal.immediate_stop);

    // A normal page transition is graceful, not an immediate stop.
    const CodexBlePageDecision ordinary_page = codex_ble_page_decision(
        kWorkPageWeatherClock,
        false,
        false,
        false,
        false,
        false,
        kOtaIdle);
    assert(!ordinary_page.should_run);
    assert(!ordinary_page.immediate_stop);

    const CodexBlePageDecision low_battery = codex_ble_page_decision(
        kWorkPageCodexUsage,
        true,
        false,
        false,
        false,
        false,
        kOtaIdle);
    assert(!low_battery.should_run);
    assert(low_battery.immediate_stop);

    const CodexBlePageDecision setup_queued = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        true,
        false,
        false,
        false,
        kOtaIdle);
    assert(!setup_queued.should_run);
    assert(setup_queued.immediate_stop);

    const CodexBlePageDecision setup_active = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        true,
        false,
        false,
        kOtaIdle);
    assert(!setup_active.should_run);
    assert(setup_active.immediate_stop);

    // Settings is a temporary overlay over CODEX and keeps the connection.
    const CodexBlePageDecision settings = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        true,
        true,
        kOtaIdle);
    assert(settings.should_run);
    assert(!settings.immediate_stop);

    // Other auxiliary pages still stop BLE.
    const CodexBlePageDecision other_auxiliary = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        true,
        false,
        kOtaIdle);
    assert(!other_auxiliary.should_run);
    assert(other_auxiliary.immediate_stop);

    // OTA stop scope is intentionally limited to Checking and Updating.
    const CodexBlePageDecision ota_checking = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        true,
        true,
        kOtaChecking);
    assert(!ota_checking.should_run);
    assert(ota_checking.immediate_stop);
    const CodexBlePageDecision ota_updating = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        true,
        true,
        kOtaUpdating);
    assert(!ota_updating.should_run);
    assert(ota_updating.immediate_stop);
    const CodexBlePageDecision ota_available = codex_ble_page_decision(
        kWorkPageCodexUsage,
        false,
        false,
        false,
        false,
        false,
        kOtaAvailable);
    assert(ota_available.should_run);
    assert(!ota_available.immediate_stop);

    // Disabling/removing CODEX changes the active work page and stops BLE.
    assert(!codex_ble_page_should_run(
        kWorkPageWeatherClock, false, false, true, true, false));
    assert(codex_ble_icon_should_show(true, true));
    assert(!codex_ble_icon_should_show(false, true));
    assert(!codex_ble_icon_should_show(true, false));
    return 0;
}
