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
    return 0;
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

    return 0;
}
