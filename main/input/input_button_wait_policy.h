// 定义空闲工作页按键任务进入 GPIO 事件等待的纯判断策略。
#pragma once

struct ButtonIdleContext {
    bool battery_charging;
    bool setup_portal_active;
    bool settings_requested;
    bool info_requested;
    bool network_diag_requested;
    bool ota_active;
    bool audio_playing;
    bool wifi_on;
    bool low_battery_mode;
    bool active_page_enabled;
};

constexpr bool button_idle_work_page_context(const ButtonIdleContext &context)
{
    return !context.battery_charging &&
           !context.setup_portal_active &&
           !context.settings_requested &&
           !context.info_requested &&
           !context.network_diag_requested &&
           !context.ota_active &&
           !context.audio_playing &&
           !context.wifi_on &&
           (context.low_battery_mode || context.active_page_enabled);
}

constexpr bool button_task_can_wait_for_edge(bool edge_wakeup_ready,
                                             bool idle_work_page,
                                             bool boot_pressed,
                                             bool key_pressed,
                                             bool press_tracking_active)
{
    return edge_wakeup_ready &&
           idle_work_page &&
           !boot_pressed &&
           !key_pressed &&
           !press_tracking_active;
}
