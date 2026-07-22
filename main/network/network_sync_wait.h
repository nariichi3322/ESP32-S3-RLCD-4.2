// 声明常驻网络同步任务的事件等待策略。
#pragma once

#include <stdint.h>

enum class NetworkSyncConnectionWaitResult {
    kConnected,
    kRuntimeChanged,
    kTimedOut,
};

void wait_for_network_sync_event(uint32_t timeout_ms);
void wait_for_setup_portal_retry();
void wait_for_network_runtime_request();
void wait_for_ota_network_block_change();
NetworkSyncConnectionWaitResult wait_for_network_sync_connection(uint32_t timeout_ms);
