// 验证网络同步任务各等待入口使用正确事件位、清位语义和超时。
#include "network_sync_wait.h"

#include "app_event_group.h"

#include <cassert>

namespace {

struct WaitCall {
    EventBits_t bits = 0;
    BaseType_t clear_on_exit = pdFALSE;
    BaseType_t wait_for_all = pdFALSE;
    TickType_t timeout = 0;
    int count = 0;
};

WaitCall s_wait_call;
EventBits_t s_wait_result = 0;

constexpr EventBits_t kExpectedSyncWakeBits = kProvisioningSyncBit |
                                               kManualNtpSyncBit |
                                               kManualWeatherSyncBit |
                                               kManualSayingSyncBit |
                                               kNetworkDiagBit |
                                               kNetworkStateChangedBit |
                                               kSetupPortalStartBit;

void reset_wait_call()
{
    s_wait_call = {};
    s_wait_result = 0;
}

void assert_wait(EventBits_t bits,
                 BaseType_t clear_on_exit,
                 TickType_t timeout)
{
    assert(s_wait_call.count == 1);
    assert(s_wait_call.bits == bits);
    assert(s_wait_call.clear_on_exit == clear_on_exit);
    assert(s_wait_call.wait_for_all == pdFALSE);
    assert(s_wait_call.timeout == timeout);
}

} // namespace

EventBits_t app_event_group_wait_bits(EventBits_t bits,
                                      BaseType_t clear_on_exit,
                                      BaseType_t wait_for_all,
                                      TickType_t timeout)
{
    s_wait_call.bits = bits;
    s_wait_call.clear_on_exit = clear_on_exit;
    s_wait_call.wait_for_all = wait_for_all;
    s_wait_call.timeout = timeout;
    ++s_wait_call.count;
    return s_wait_result;
}

int main()
{
    reset_wait_call();
    wait_for_network_sync_event(4321);
    assert_wait(kExpectedSyncWakeBits, pdFALSE, pdMS_TO_TICKS(4321));

    reset_wait_call();
    wait_for_setup_portal_retry();
    assert_wait(kExpectedSyncWakeBits & ~kSetupPortalStartBit,
                pdFALSE,
                pdMS_TO_TICKS(1000));

    reset_wait_call();
    wait_for_network_runtime_request();
    assert_wait(kExpectedSyncWakeBits, pdFALSE, portMAX_DELAY);

    reset_wait_call();
    wait_for_ota_network_block_change();
    assert_wait(kNetworkStateChangedBit, pdTRUE, portMAX_DELAY);

    reset_wait_call();
    s_wait_result = kWifiConnectedBit;
    assert(wait_for_network_sync_connection(45000) ==
           NetworkSyncConnectionWaitResult::kConnected);
    assert_wait(kWifiConnectedBit | kNetworkStateChangedBit,
                pdFALSE,
                pdMS_TO_TICKS(45000));

    reset_wait_call();
    s_wait_result = kNetworkStateChangedBit;
    assert(wait_for_network_sync_connection(1234) ==
           NetworkSyncConnectionWaitResult::kRuntimeChanged);
    assert_wait(kWifiConnectedBit | kNetworkStateChangedBit,
                pdFALSE,
                pdMS_TO_TICKS(1234));

    reset_wait_call();
    s_wait_result = kWifiConnectedBit | kNetworkStateChangedBit;
    assert(wait_for_network_sync_connection(77) ==
           NetworkSyncConnectionWaitResult::kRuntimeChanged);

    reset_wait_call();
    assert(wait_for_network_sync_connection(9) ==
           NetworkSyncConnectionWaitResult::kTimedOut);
    assert_wait(kWifiConnectedBit | kNetworkStateChangedBit,
                pdFALSE,
                pdMS_TO_TICKS(9));

    return 0;
}
