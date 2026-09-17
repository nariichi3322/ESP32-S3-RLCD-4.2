// 定义按键 GPIO 初始化重试和松开后事件等待的纯判断策略。
#pragma once

#include "input_button_config.h"

#include <type_traits>

constexpr unsigned kButtonGpioConfigMaxAttempts = 3;
constexpr int kButtonGpioConfigRetryDelayMs = 100;

constexpr bool button_gpio_config_retry_due(unsigned completed_attempt,
                                            bool succeeded)
{
    return !succeeded && completed_attempt < kButtonGpioConfigMaxAttempts;
}

constexpr bool button_task_can_wait_for_edge(bool edge_wakeup_ready,
                                             bool boot_pressed,
                                             bool key_pressed,
                                             bool press_tracking_active,
                                             bool wake_window_active = false)
{
    return edge_wakeup_ready &&
           !boot_pressed &&
           !key_pressed &&
           !press_tracking_active &&
           !wake_window_active;
}

enum class ButtonTaskWaitMode {
    kPollingDelay,
    kTimedNotification,
    kIndefiniteNotification,
};

constexpr ButtonTaskWaitMode button_task_wait_mode(bool edge_wakeup_ready,
                                                   bool boot_pressed,
                                                   bool key_pressed,
                                                   bool press_tracking_active,
                                                   bool wake_window_active)
{
    if (button_task_can_wait_for_edge(edge_wakeup_ready,
                                      boot_pressed,
                                      key_pressed,
                                      press_tracking_active,
                                      wake_window_active)) {
        return ButtonTaskWaitMode::kIndefiniteNotification;
    }
    return edge_wakeup_ready ? ButtonTaskWaitMode::kTimedNotification
                             : ButtonTaskWaitMode::kPollingDelay;
}

template <typename Tick>
constexpr Tick button_task_wait_ticks(Tick fallback_ticks,
                                      bool wake_window_active,
                                      Tick wake_window_remaining_ticks)
{
    static_assert(std::is_integral<Tick>::value && std::is_unsigned<Tick>::value,
                  "Tick must be an unsigned integral type");
    const Tick safe_fallback_ticks =
        fallback_ticks == 0 ? static_cast<Tick>(1) : fallback_ticks;
    if (!wake_window_active) {
        return safe_fallback_ticks;
    }
    if (wake_window_remaining_ticks == 0) {
        return static_cast<Tick>(1);
    }
    return wake_window_remaining_ticks < safe_fallback_ticks
               ? wake_window_remaining_ticks
               : safe_fallback_ticks;
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
