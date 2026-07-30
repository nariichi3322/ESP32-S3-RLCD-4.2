// 定义按键松开时进入 GPIO 事件等待的纯判断策略。
#pragma once

#include "input_button_config.h"

constexpr bool button_task_can_wait_for_edge(bool edge_wakeup_ready,
                                             bool boot_pressed,
                                             bool key_pressed,
                                             bool press_tracking_active)
{
    return edge_wakeup_ready &&
           !boot_pressed &&
           !key_pressed &&
           !press_tracking_active;
}

constexpr int button_task_poll_delay_ms(bool any_button_pressed,
                                        bool interactive_surface,
                                        bool low_refresh_surface)
{
    if (any_button_pressed) {
        return kButtonPressedPollMs;
    }
    if (interactive_surface) {
        return kButtonActivePollMs;
    }
    if (low_refresh_surface) {
        return kButtonLowRefreshIdlePollMs;
    }
    return kButtonIdlePollMs;
}
