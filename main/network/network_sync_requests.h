// 声明网络同步请求快照、设置反馈和事件位收尾接口。
#pragma once

#include "ui_settings_contract.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

struct NetworkSyncRequestSnapshot {
    bool provisioning = false;
    bool manual_ntp = false;
    bool manual_weather = false;
    bool manual_saying = false;
    bool visible_weather = false;
    bool visible_saying = false;
    bool diagnostics = false;

    bool weather_due() const
    {
        return manual_weather || visible_weather;
    }

    bool saying_due() const
    {
        return manual_saying || visible_saying;
    }

    bool none_for_setup_portal() const
    {
        return !provisioning && !manual_ntp && !manual_weather &&
               !manual_saying && !visible_weather && !visible_saying &&
               !diagnostics;
    }
};

NetworkSyncRequestSnapshot snapshot_network_sync_requests();
EventBits_t network_sync_request_bits(const NetworkSyncRequestSnapshot &requests);
void clear_network_request_bits();
void finish_settings_sync_and_clear_bit(SettingsSyncOp op,
                                        const char *status,
                                        EventBits_t bit);
void set_network_diag_unavailable(const char *ip_location_text);
void finish_offline_network_requests(const NetworkSyncRequestSnapshot &requests);
void finish_unconfigured_network_requests(const NetworkSyncRequestSnapshot &requests);
void finish_low_battery_network_requests(const NetworkSyncRequestSnapshot &requests);
void finish_failed_sync_requests(const NetworkSyncRequestSnapshot &requests);
void finish_successful_sync_requests(const NetworkSyncRequestSnapshot &requests,
                                     bool ntp_ok,
                                     bool weather_ok,
                                     bool saying_ok);
void finish_network_diagnostics_sync();
