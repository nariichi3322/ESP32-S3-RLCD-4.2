// 集中发布关于本机页面状态，避免 UI、输入和 OTA 读取到混合字段。
#include "ui_info_page_state.h"

#include "scoped_semaphore_lock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
StaticSemaphore_t s_info_page_mutex_storage = {};
SemaphoreHandle_t s_info_page_mutex = nullptr;
InfoPageStateSnapshot s_info_page_state;
}

bool info_page_state_init()
{
    if (s_info_page_mutex) {
        return true;
    }
    s_info_page_mutex = xSemaphoreCreateMutexStatic(&s_info_page_mutex_storage);
    return s_info_page_mutex != nullptr;
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
    InfoPageStateSnapshot state = {};
    info_page_state_load(&state);
    return state.requested;
}

void info_page_request(uint32_t hold_until_tick)
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state.requested = true;
    s_info_page_state.hold_until_tick = hold_until_tick;
}

void info_page_clear()
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state = {};
}

void info_page_hold_until_store(uint32_t hold_until_tick)
{
    ScopedSemaphoreLock lock(s_info_page_mutex);
    if (!lock) {
        return;
    }
    s_info_page_state.hold_until_tick = hold_until_tick;
}
