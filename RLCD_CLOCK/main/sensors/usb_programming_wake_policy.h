// 定義 USB 燒錄喚醒窗口的固定期限與回繞安全純判斷規則。
#pragma once

#include "app_tick_time.h"

#include <stdint.h>
#include <type_traits>

inline constexpr uint32_t kUsbProgrammingWakeWindowMs = 60U * 1000U;

template <typename Tick>
constexpr Tick usb_programming_wake_window_deadline(Tick now,
                                                   Tick window_ticks)
{
    static_assert(std::is_integral<Tick>::value && std::is_unsigned<Tick>::value,
                  "Tick must be an unsigned integral type");
    return static_cast<Tick>(now + window_ticks);
}

template <typename Tick>
constexpr bool usb_programming_wake_window_pending(bool requested,
                                                   Tick now,
                                                   Tick deadline)
{
    return requested && app_tick_deadline_pending(now, deadline);
}

template <typename Tick>
constexpr Tick usb_programming_wake_window_remaining_ticks(bool requested,
                                                           Tick now,
                                                           Tick deadline)
{
    return requested
               ? app_tick_deadline_remaining(now, deadline)
               : static_cast<Tick>(0);
}
