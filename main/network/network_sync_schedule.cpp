// 计算联网同步任务本轮应执行的项目，不访问 Wi-Fi、事件组或全局状态。
#include "network_sync_schedule.h"

#include <limits.h>

namespace {
constexpr int64_t kMicrosecondsPerMillisecond = 1000;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr time_t kSecondsPerMinute = 60;
constexpr uint32_t kIdleDefaultWaitMs = 5 * kSecondsPerMinute * kMillisecondsPerSecond;
constexpr uint32_t kIdleMinimumWaitMs = 1000;
static_assert(kIdleMinimumWaitMs > 0, "network idle minimum wait must be positive");
static_assert(kIdleDefaultWaitMs >= kIdleMinimumWaitMs,
              "network idle default wait must cover the minimum wait");

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

uint32_t network_idle_wait_ms(time_t now,
                              time_t next_boot_due_at,
                              time_t next_ntp_retry_at)
{
    uint32_t wait_ms = kIdleDefaultWaitMs;
    if (next_boot_due_at > now) {
        uint32_t boot_wait =
            static_cast<uint32_t>((next_boot_due_at - now) * kMillisecondsPerSecond);
        if (boot_wait < wait_ms) {
            wait_ms = boot_wait;
        }
    }
    if (next_ntp_retry_at > now) {
        uint32_t ntp_wait =
            static_cast<uint32_t>((next_ntp_retry_at - now) * kMillisecondsPerSecond);
        if (ntp_wait < wait_ms) {
            wait_ms = ntp_wait;
        }
    }
    if (wait_ms < kIdleMinimumWaitMs) {
        wait_ms = kIdleMinimumWaitMs;
    } else if (wait_ms > kIdleDefaultWaitMs) {
        wait_ms = kIdleDefaultWaitMs;
    }
    return wait_ms;
}

bool network_cache_age_is_fresh(time_t now, time_t cached_at, time_t max_age)
{
    return cached_at > 0 && max_age > 0 && now >= cached_at &&
           now - cached_at < max_age;
}

bool network_cache_local_day_matches(const struct tm &now_local,
                                     const struct tm &cached_local)
{
    return now_local.tm_year == cached_local.tm_year &&
           now_local.tm_yday == cached_local.tm_yday;
}

bool network_cache_local_hour_matches(const struct tm &now_local,
                                      const struct tm &cached_local)
{
    return network_cache_local_day_matches(now_local, cached_local) &&
           now_local.tm_hour == cached_local.tm_hour;
}
