// 管理小智语音番茄钟的单调计时、后台完成提醒和 UI 快照。
#pragma once

#include <stdint.h>

enum PomodoroState {
    kPomodoroIdle = 0,
    kPomodoroRunning,
    kPomodoroCompleted,
};

struct PomodoroSnapshot {
    PomodoroState state;
    uint32_t total_ms;
    uint32_t remaining_ms;
    bool alerting;
    uint32_t version;
};

void pomodoro_services_init();
void pomodoro_task(void *);
void pomodoro_get_snapshot(PomodoroSnapshot *out);
bool pomodoro_is_running();
bool pomodoro_start(uint32_t duration_seconds);
bool pomodoro_cancel();
bool pomodoro_stop_alert_from_button();
