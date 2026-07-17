// 声明单次闹钟运行态与覆盖确认状态的一致快照接口。
#pragma once

#include "alarm_replacement_policy.h"
#include "alarm_services.h"

#include <stdint.h>

bool alarm_runtime_state_init();
bool alarm_runtime_publish(bool enabled,
                           bool ringing,
                           int hour,
                           int minute);
bool alarm_runtime_snapshot(AlarmSnapshot *out);
bool alarm_runtime_is_enabled();
bool alarm_runtime_clear_replacement();
AlarmReplacementDecision alarm_runtime_replacement_decision(
    int requested_hour,
    int requested_minute,
    bool confirmed,
    uint32_t now_ms,
    uint32_t timeout_ms,
    AlarmSnapshot *existing);
