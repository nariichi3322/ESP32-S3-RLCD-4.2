// 集中发布关于本机页面状态，避免 UI、输入和 OTA 读取到混合字段。
#include "ui_info_page_state.h"

#include "scoped_semaphore_lock.h"

#include <atomic>

namespace {
StaticTaskMutex s_info_page_mutex;
InfoPageStateSnapshot s_info_page_state;
std::atomic<bool> s_info_page_requested{false};
}

bool info_page_state_init()
{
    return s_info_page_mutex.init();
}

void info_page_state_load(InfoPageStateSnapshot *out)
{
    if (!out) {
        return;
    }
    *out = {};
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    *out = s_info_page_state;
}

bool info_page_requested()
{
    return s_info_page_requested.load(std::memory_order_acquire);
}

void info_page_request(uint32_t hold_until_tick)
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state.requested = true;
    s_info_page_state.hold_until_tick = hold_until_tick;
    s_info_page_requested.store(true, std::memory_order_release);
}

void info_page_clear()
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state = {};
    s_info_page_requested.store(false, std::memory_order_release);
}

void info_page_hold_until_store(uint32_t hold_until_tick)
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state.hold_until_tick = hold_until_tick;
}
