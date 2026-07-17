// 发布只用于唤醒后台网络调度器的运行态变化事件。
#include "network_runtime_events.h"

#include "app_event_group.h"

void notify_network_sync_runtime_state_changed()
{
    if (app_event_group_ready()) {
        app_event_group_set_bits(kNetworkStateChangedBit);
    }
}
