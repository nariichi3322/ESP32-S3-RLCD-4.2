// 声明常驻网络同步任务的事件等待策略。
#pragma once

#include <stdint.h>

struct NetworkSyncAvailability;

enum class NetworkSyncConnectionWaitResult {
    kConnected,
    kRuntimeChanged,
    kTimedOut,
};

enum class NetworkSyncCompletionWaitResult {
    kCompleted,
    kRuntimeChanged,
    kTimedOut,
};

void wait_for_network_sync_event(uint32_t timeout_ms);
void wait_for_setup_portal_retry();
void wait_for_active_setup_portal_request();
void wait_for_network_runtime_request();
void wait_for_ota_network_block_change();
bool wait_for_network_sync_settle(uint32_t timeout_ms);
NetworkSyncConnectionWaitResult wait_for_network_sync_connection(uint32_t timeout_ms);
NetworkSyncConnectionWaitResult wait_for_valid_network_sync_connection(
    const NetworkSyncAvailability &scheduled_runtime,
    uint32_t scheduled_request_bits,
    uint32_t timeout_ms);
NetworkSyncCompletionWaitResult wait_for_ntp_sync_completion(uint32_t timeout_ms);
