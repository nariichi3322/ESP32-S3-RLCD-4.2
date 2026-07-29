// 使用静态任务互斥发布闹钟状态和覆盖确认，避免普通任务复制状态时关闭跨核中断。
#include "alarm_runtime_state_internal.h"

#include "scoped_semaphore_lock.h"

#include <atomic>

namespace {
StaticTaskMutex s_runtime_mutex;
AlarmSnapshot s_alarm = {false, false, 0, 0};
AlarmReplacementConfirmation s_replacement_confirmation = {};
std::atomic<bool> s_enabled{false};
uint32_t s_generation = 1;
}

bool alarm_runtime_state_init()
{
    if (!s_runtime_mutex.init()) {
        return false;
    }
    return true;
}

bool alarm_runtime_publish(bool enabled,
                           bool ringing,
                           int hour,
                           int minute)
{
    ScopedSemaphoreLock lock(s_runtime_mutex);
    if (!lock) {
        return false;
    }
    s_alarm.enabled = enabled;
    s_alarm.ringing = ringing;
    s_alarm.hour = static_cast<uint8_t>(hour);
    s_alarm.minute = static_cast<uint8_t>(minute);
    ++s_generation;
    s_enabled.store(enabled, std::memory_order_release);
    return true;
}

bool alarm_runtime_snapshot(AlarmSnapshot *out)
{
    if (!out) {
        return false;
    }
    *out = {};
    ScopedSemaphoreLock lock(s_runtime_mutex);
    if (!lock) {
        return false;
    }
    *out = s_alarm;
    return true;
}

bool alarm_runtime_is_enabled()
{
    return s_enabled.load(std::memory_order_acquire);
}

bool alarm_runtime_clear_replacement()
{
    ScopedSemaphoreLock lock(s_runtime_mutex);
    if (!lock) {
        return false;
    }
    clear_alarm_replacement_confirmation(&s_replacement_confirmation);
    return true;
}

AlarmReplacementDecision alarm_runtime_replacement_decision(
    int requested_hour,
    int requested_minute,
    bool confirmed,
    uint32_t now_ms,
    uint32_t timeout_ms,
    AlarmSnapshot *existing)
{
    ScopedSemaphoreLock lock(s_runtime_mutex);
    if (!lock) {
        if (existing) {
            *existing = {};
        }
        return kAlarmReplacementConfirmationInvalid;
    }
    if (existing) {
        *existing = s_alarm;
    }
    return evaluate_alarm_replacement(s_alarm.enabled,
                                      s_alarm.hour,
                                      s_alarm.minute,
                                      s_generation,
                                      requested_hour,
                                      requested_minute,
                                      confirmed,
                                      now_ms,
                                      timeout_ms,
                                      &s_replacement_confirmation);
}
