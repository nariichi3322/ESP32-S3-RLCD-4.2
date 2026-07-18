// 集中发布电池相关字段，避免采样任务与消费者读取到混合状态。
#include "battery_runtime_state.h"

#include "scoped_semaphore_lock.h"

#include <atomic>

namespace {
StaticTaskMutex s_battery_runtime_mutex;
BatteryRuntimeSnapshot s_battery_runtime;
std::atomic<int> s_battery_percent{-1};
std::atomic<bool> s_battery_charging{false};
std::atomic<bool> s_battery_low_mode{false};
}

bool battery_runtime_state_init()
{
    return s_battery_runtime_mutex.init();
}

void battery_runtime_snapshot_load(BatteryRuntimeSnapshot *out)
{
    if (!out) {
        return;
    }
    ScopedSemaphoreLock lock(s_battery_runtime_mutex.handle());
    if (!lock) {
        *out = BatteryRuntimeSnapshot{};
        return;
    }
    *out = s_battery_runtime;
}

void battery_runtime_snapshot_store(const BatteryRuntimeSnapshot &snapshot)
{
    ScopedSemaphoreLock lock(s_battery_runtime_mutex.handle());
    if (!lock) {
        return;
    }
    s_battery_runtime = snapshot;
    s_battery_percent.store(snapshot.percent, std::memory_order_release);
    s_battery_charging.store(snapshot.charging, std::memory_order_release);
    s_battery_low_mode.store(snapshot.low_battery_mode, std::memory_order_release);
}

void battery_runtime_voltage_store(float voltage)
{
    ScopedSemaphoreLock lock(s_battery_runtime_mutex.handle());
    if (!lock) {
        return;
    }
    s_battery_runtime.voltage = voltage;
}

int battery_percent_load()
{
    return s_battery_percent.load(std::memory_order_acquire);
}

bool battery_charging_load()
{
    return s_battery_charging.load(std::memory_order_acquire);
}

bool battery_low_mode_load()
{
    return s_battery_low_mode.load(std::memory_order_acquire);
}

bool battery_low_mode_for_percent(bool current_mode,
                                  int percent,
                                  int enter_percent,
                                  int exit_percent)
{
    if (percent < 0) {
        return current_mode;
    }
    if (!current_mode && percent < enter_percent) {
        return true;
    }
    if (current_mode && percent >= exit_percent) {
        return false;
    }
    return current_mode;
}

bool battery_runtime_low_mode_update(int enter_percent, int exit_percent)
{
    ScopedSemaphoreLock lock(s_battery_runtime_mutex.handle());
    if (!lock) {
        return false;
    }
    bool next_mode = battery_low_mode_for_percent(s_battery_runtime.low_battery_mode,
                                                  s_battery_runtime.percent,
                                                  enter_percent,
                                                  exit_percent);
    bool changed = next_mode != s_battery_runtime.low_battery_mode;
    s_battery_runtime.low_battery_mode = next_mode;
    s_battery_low_mode.store(next_mode, std::memory_order_release);
    return changed;
}
