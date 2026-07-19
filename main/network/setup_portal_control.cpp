// 负责配网热点启动请求的 EventGroup 发布、查询和日志。
#include "setup_portal_control.h"

#include "app_event_group.h"
#include "app_metadata.h"

#include <esp_log.h>

namespace {
constexpr const char *kSetupPortalStartRequestedLog =
    "setup portal start queued behind active network work";
} // namespace

bool request_setup_portal_start()
{
    if (!app_event_group_ready()) {
        return false;
    }
    app_event_group_set_bits(kSetupPortalStartBit | kNetworkStateChangedBit);
    ESP_LOGI(TAG, "%s", kSetupPortalStartRequestedLog);
    return true;
}

bool setup_portal_start_requested()
{
    return app_event_group_ready() &&
           (app_event_group_get_bits() & kSetupPortalStartBit) != 0;
}
