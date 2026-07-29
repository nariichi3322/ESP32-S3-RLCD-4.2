// 声明 Wi-Fi 射频和连接等待的轻量生命周期接口。
#pragma once

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
bool wait_for_wifi_connected(uint32_t timeout_ms, uint32_t cancel_bits = 0);
void init_wifi();
