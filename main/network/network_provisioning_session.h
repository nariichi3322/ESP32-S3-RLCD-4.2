// 声明配网热点启动与保存结果交接的运行期事务接口。
#pragma once

#include "wifi_portal_state.h"

class NetworkAwakeLockGuard;

enum class SetupPortalStartResult {
    kNoRequest,
    kStarted,
    kRetryPending,
};

SetupPortalStartResult service_setup_portal_start_request();
void wait_for_provisioning_result_feedback();
void keep_setup_portal_after_provisioning_failure(
    NetworkAwakeLockGuard &awake_lock,
    WifiPortalSaveResult result);
