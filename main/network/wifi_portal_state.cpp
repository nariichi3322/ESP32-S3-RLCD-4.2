// 集中维护配网页活跃状态和最近一次 Wi-Fi 断线原因。
#include "wifi_portal_state.h"

#include <atomic>

namespace {
std::atomic<bool> s_setup_portal_active{false};
std::atomic<int> s_last_wifi_disconnect_reason{0};
} // namespace

bool setup_portal_active_load()
{
    return s_setup_portal_active.load(std::memory_order_acquire);
}

void setup_portal_active_store(bool active)
{
    s_setup_portal_active.store(active, std::memory_order_release);
}

int wifi_last_disconnect_reason()
{
    return s_last_wifi_disconnect_reason.load(std::memory_order_acquire);
}

void record_wifi_disconnect_reason(int reason)
{
    s_last_wifi_disconnect_reason.store(reason, std::memory_order_release);
}

void clear_wifi_last_disconnect_reason()
{
    record_wifi_disconnect_reason(0);
}
