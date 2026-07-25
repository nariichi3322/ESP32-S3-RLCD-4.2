// 声明 Wi-Fi 射频、连接等待和事件回调的轻量生命周期接口。
#pragma once

#include "esp_event_base.h"

#include <stdint.h>

enum class WifiRadioIdleStopResult {
    kNoRequest,
    kDeferred,
    kStopped,
    kRetryRequired,
};

bool start_wifi_radio(bool enable_setup_portal);
void stop_wifi_radio(bool force_setup_portal = false);
void request_wifi_radio_stop_when_idle();
WifiRadioIdleStopResult service_wifi_radio_stop_when_idle();
bool apply_station_config(bool reconnect);
bool wait_for_wifi_connected(uint32_t timeout_ms);
void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data);
void init_wifi();
