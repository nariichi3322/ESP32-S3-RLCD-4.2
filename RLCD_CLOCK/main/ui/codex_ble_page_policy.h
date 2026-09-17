#pragma once

#include "ota_flow_policy.h"
#include "work_page_ids.h"

struct CodexBlePageDecision {
    bool should_run = false;
    bool immediate_stop = false;
};

constexpr CodexBlePageDecision codex_ble_page_decision(
    int active_page,
    bool low_battery,
    bool setup_portal_start_requested,
    bool setup_portal_active,
    bool auxiliary_page,
    bool settings_page,
    int ota_state)
{
    const bool ota_immediate_stop =
        ota_blocks_background_network_sync(ota_state);
    const bool immediate_stop =
        low_battery || setup_portal_start_requested || setup_portal_active ||
        ota_immediate_stop || (auxiliary_page && !settings_page);
    const bool should_run = active_page == kWorkPageCodexUsage &&
                            !immediate_stop &&
                            (!auxiliary_page || settings_page);
    return {should_run, immediate_stop};
}

constexpr bool codex_ble_page_should_run(
    int active_page,
    bool low_battery,
    bool setup_portal_start_requested,
    bool setup_portal_active,
    bool auxiliary_page,
    bool settings_page,
    int ota_state)
{
    return codex_ble_page_decision(active_page,
                                   low_battery,
                                   setup_portal_start_requested,
                                   setup_portal_active,
                                   auxiliary_page,
                                   settings_page,
                                   ota_state)
        .should_run;
}

constexpr bool codex_ble_page_should_stop_immediately(
    int active_page,
    bool low_battery,
    bool setup_portal_start_requested,
    bool setup_portal_active,
    bool auxiliary_page,
    bool settings_page,
    int ota_state)
{
    return codex_ble_page_decision(active_page,
                                   low_battery,
                                   setup_portal_start_requested,
                                   setup_portal_active,
                                   auxiliary_page,
                                   settings_page,
                                   ota_state)
        .immediate_stop;
}

// Compatibility overload for callers that only have the previous boolean OTA
// updating flag.  New policy callers should pass the concrete OTA state above
// so kOtaChecking and kOtaUpdating remain the only OTA stop states.
constexpr bool codex_ble_page_should_run(int active_page,
                                         bool low_battery,
                                         bool setup_portal,
                                         bool auxiliary_page,
                                         bool settings_page,
                                         bool ota_updating)
{
    return codex_ble_page_should_run(active_page,
                                     low_battery,
                                     false,
                                     setup_portal,
                                     auxiliary_page,
                                     settings_page,
                                     ota_updating ? kOtaUpdating : kOtaIdle);
}

constexpr bool codex_ble_icon_should_show(bool normal_work_surface,
                                          bool transport_running)
{
    return normal_work_surface && transport_running;
}
