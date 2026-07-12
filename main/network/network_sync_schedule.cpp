// 计算联网同步任务本轮应执行的项目，不访问 Wi-Fi、事件组或全局状态。
#include "network_sync_schedule.h"

#include <limits.h>

namespace {
constexpr int64_t kMicrosecondsPerMillisecond = 1000;

time_t earliest_pending_boot_sync(const NetworkSyncScheduleInput &input)
{
    time_t next = 0;
    if (input.boot_weather_due && input.boot_weather_due_at > input.now) {
        next = input.boot_weather_due_at;
    }
    if (input.boot_saying_due &&
        input.boot_saying_due_at > input.now &&
        (next == 0 || input.boot_saying_due_at < next)) {
        next = input.boot_saying_due_at;
    }
    return next;
}
} // namespace

NetworkSyncSchedule calculate_network_sync_schedule(const NetworkSyncScheduleInput &input)
{
    NetworkSyncSchedule schedule = {};
    schedule.boot_weather_ready = input.boot_weather_due && input.now >= input.boot_weather_due_at;
    schedule.boot_saying_ready = input.boot_saying_due && input.now >= input.boot_saying_due_at;
    schedule.weather_due = input.have_weather_key &&
                           !input.low_battery_mode &&
                           (input.manual_weather_due ||
                            input.provisioning_sync_due ||
                            schedule.boot_weather_ready);
    schedule.ntp_due = (input.manual_ntp_due ||
                        input.provisioning_sync_due ||
                        input.boot_ntp_due ||
                        input.midnight_ntp_due) &&
                       input.now >= input.next_ntp_retry_at;
    schedule.saying_due = !input.low_battery_mode &&
                          (input.manual_saying_due ||
                           input.provisioning_sync_due ||
                           schedule.boot_saying_ready);
    schedule.next_boot_due_at = earliest_pending_boot_sync(input);
    return schedule;
}

int network_boot_budget_remaining_ms(int64_t deadline_us, int64_t now_us)
{
    if (deadline_us <= 0) {
        return INT32_MAX;
    }
    int64_t remaining_us = deadline_us - now_us;
    if (remaining_us <= 0) {
        return 0;
    }
    int64_t remaining_ms = remaining_us / kMicrosecondsPerMillisecond;
    return remaining_ms > INT32_MAX ? INT32_MAX : static_cast<int>(remaining_ms);
}
