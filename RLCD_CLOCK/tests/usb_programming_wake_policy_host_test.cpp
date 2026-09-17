// 驗證 USB 燒錄喚醒窗口的固定期限、重設與 Tick 回繞安全判斷。
#include "usb_programming_wake_policy.h"

#include <assert.h>
#include <stdint.h>

int main()
{
    using Tick = uint32_t;
    static_assert(kUsbProgrammingWakeWindowMs == 60000,
                  "USB programming wake window must remain 60 seconds");

    constexpr Tick kWindowTicks = 600;
    constexpr Tick kFirstPress = 100;
    constexpr Tick kFirstDeadline =
        usb_programming_wake_window_deadline(kFirstPress, kWindowTicks);
    assert(usb_programming_wake_window_pending(
        true, kFirstPress, kFirstDeadline));
    assert(usb_programming_wake_window_pending(
        true, kFirstDeadline - 1, kFirstDeadline));
    assert(!usb_programming_wake_window_pending(
        true, kFirstDeadline, kFirstDeadline));
    assert(!usb_programming_wake_window_pending(
        true, kFirstDeadline + 1, kFirstDeadline));
    assert(usb_programming_wake_window_remaining_ticks(
               true, kFirstPress, kFirstDeadline) == kWindowTicks);
    assert(usb_programming_wake_window_remaining_ticks(
               true, kFirstDeadline - 1, kFirstDeadline) == 1);
    assert(usb_programming_wake_window_remaining_ticks(
               true, kFirstDeadline, kFirstDeadline) == 0);
    assert(usb_programming_wake_window_remaining_ticks(
               false, kFirstPress, kFirstDeadline) == 0);

    constexpr Tick kRepeatedPress = kFirstPress + 40;
    constexpr Tick kRepeatedDeadline =
        usb_programming_wake_window_deadline(kRepeatedPress, kWindowTicks);
    assert(usb_programming_wake_window_pending(
        true, kFirstDeadline, kRepeatedDeadline));
    assert(!usb_programming_wake_window_pending(
        true, kRepeatedDeadline, kRepeatedDeadline));

    constexpr Tick kWrapPress = UINT32_MAX - 5;
    constexpr Tick kWrapWindowTicks = 10;
    constexpr Tick kWrapDeadline =
        usb_programming_wake_window_deadline(kWrapPress, kWrapWindowTicks);
    assert(kWrapDeadline == 4);
    assert(usb_programming_wake_window_pending(
        true, kWrapPress, kWrapDeadline));
    assert(usb_programming_wake_window_pending(
        true, static_cast<Tick>(kWrapPress + 9), kWrapDeadline));
    assert(!usb_programming_wake_window_pending(
        true, kWrapDeadline, kWrapDeadline));
    return 0;
}
