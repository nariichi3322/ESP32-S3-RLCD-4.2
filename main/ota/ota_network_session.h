// 声明 OTA 专用 Wi-Fi 与网络唤醒锁会话的启动和收尾接口。
#pragma once

#include "ota_flow_policy.h"

#include <stdint.h>

enum class OtaNetworkSessionStartResult {
    kReady,
    kOfflineMode,
    kNoWifi,
    kLowBattery,
    kSetupPortal,
    kAwakeLockUnavailable,
    kRadioStartFailed,
    kConnectionTimedOut,
};

constexpr OtaNetworkSessionStartResult
ota_network_session_start_result_for_block(OtaNetworkBlockReason reason)
{
    switch (reason) {
    case OtaNetworkBlockReason::kOfflineMode:
        return OtaNetworkSessionStartResult::kOfflineMode;
    case OtaNetworkBlockReason::kNoWifi:
        return OtaNetworkSessionStartResult::kNoWifi;
    case OtaNetworkBlockReason::kLowBattery:
        return OtaNetworkSessionStartResult::kLowBattery;
    case OtaNetworkBlockReason::kSetupPortal:
        return OtaNetworkSessionStartResult::kSetupPortal;
    case OtaNetworkBlockReason::kNone:
        return OtaNetworkSessionStartResult::kReady;
    }
    return OtaNetworkSessionStartResult::kConnectionTimedOut;
}

OtaNetworkSessionStartResult ota_network_session_start(uint32_t timeout_ms);
void ota_network_session_finish(OtaWifiFinishPolicy policy);
