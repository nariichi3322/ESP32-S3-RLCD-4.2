// 实现常驻网络同步任务的普通、退避和状态变化等待语义。
#include "network_sync_wait.h"

#include "app_event_group.h"

namespace {

constexpr uint32_t kSetupPortalRetryWaitMs = 1000;
constexpr EventBits_t kNetworkSyncWakeBits = kProvisioningSyncBit |
                                             kManualNtpSyncBit |
                                             kManualWeatherSyncBit |
                                             kManualSayingSyncBit |
                                             kVisibleWeatherSyncBit |
                                             kVisibleSayingSyncBit |
                                             kNetworkDiagBit |
                                             kNetworkStateChangedBit |
                                             kSetupPortalStartBit;
constexpr EventBits_t kSetupPortalRetryWakeBits =
    kNetworkSyncWakeBits & ~kSetupPortalStartBit;
constexpr EventBits_t kNetworkConnectionWakeBits =
    kWifiConnectedBit | kNetworkStateChangedBit;
static_assert((kNetworkSyncWakeBits & kNetworkStateChangedBit) != 0,
              "network sync wait must wake on runtime state changes");
static_assert((kSetupPortalRetryWakeBits & kSetupPortalStartBit) == 0,
              "setup portal retry wait must ignore its pending level bit");
static_assert((kNetworkConnectionWakeBits & kWifiConnectedBit) != 0 &&
                  (kNetworkConnectionWakeBits & kNetworkStateChangedBit) != 0,
              "connection wait must observe success and runtime cancellation");

} // namespace

void wait_for_network_sync_event(uint32_t timeout_ms)
{
    app_event_group_wait_bits(kNetworkSyncWakeBits,
                              pdFALSE,
                              pdFALSE,
                              pdMS_TO_TICKS(timeout_ms));
}

void wait_for_setup_portal_retry()
{
    // kSetupPortalStartBit remains set until the AP starts successfully. Do
    // not wait on that same level-triggered bit or the task will wake itself
    // immediately and spin through Wi-Fi start attempts without backoff.
    app_event_group_wait_bits(kSetupPortalRetryWakeBits,
                              pdFALSE,
                              pdFALSE,
                              pdMS_TO_TICKS(kSetupPortalRetryWaitMs));
}

void wait_for_network_runtime_request()
{
    app_event_group_wait_bits(kNetworkSyncWakeBits,
                              pdFALSE,
                              pdFALSE,
                              portMAX_DELAY);
}

void wait_for_ota_network_block_change()
{
    // Pending level-triggered sync bits remain queued while OTA owns HTTPS and
    // Wi-Fi. Wait only for the edge-like runtime-state bit so those requests do
    // not turn the protection branch into a busy loop.
    app_event_group_wait_bits(kNetworkStateChangedBit,
                              pdTRUE,
                              pdFALSE,
                              portMAX_DELAY);
}

NetworkSyncConnectionWaitResult wait_for_network_sync_connection(uint32_t timeout_ms)
{
    const EventBits_t bits = app_event_group_wait_bits(
        kNetworkConnectionWakeBits,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    if ((bits & kNetworkStateChangedBit) != 0) {
        return NetworkSyncConnectionWaitResult::kRuntimeChanged;
    }
    if ((bits & kWifiConnectedBit) != 0) {
        return NetworkSyncConnectionWaitResult::kConnected;
    }
    return NetworkSyncConnectionWaitResult::kTimedOut;
}
