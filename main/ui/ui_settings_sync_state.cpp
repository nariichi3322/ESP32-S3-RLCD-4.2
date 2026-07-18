// 集中发布设置页手动同步状态，避免网络与 UI 读取到混合字段。
#include "ui_settings_sync_state.h"

#include "scoped_semaphore_lock.h"

namespace {
StaticTaskMutex s_settings_sync_mutex;
SettingsSyncStateSnapshot s_settings_sync_state;
}

bool settings_sync_state_init()
{
    return s_settings_sync_mutex.init();
}

void settings_sync_state_load(SettingsSyncStateSnapshot *out)
{
    if (!out) {
        return;
    }
    *out = {};
    ScopedSemaphoreLock lock(s_settings_sync_mutex.handle());
    if (!lock) {
        return;
    }
    *out = s_settings_sync_state;
}

void settings_sync_state_begin(int operation, uint32_t deadline_tick)
{
    ScopedSemaphoreLock lock(s_settings_sync_mutex.handle());
    if (!lock) {
        return;
    }
    s_settings_sync_state.operation = operation;
    s_settings_sync_state.deadline_tick = deadline_tick;
}

bool settings_sync_state_clear_if(int operation)
{
    ScopedSemaphoreLock lock(s_settings_sync_mutex.handle());
    if (!lock) {
        return false;
    }
    bool matched = s_settings_sync_state.operation == operation;
    if (matched) {
        s_settings_sync_state = {};
    }
    return matched;
}
