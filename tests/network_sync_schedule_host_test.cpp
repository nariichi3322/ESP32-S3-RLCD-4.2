// 验证联网同步错峰、手动请求、低电量和 NTP 退避的纯调度规则。
#include "network_sync_schedule.h"

#include <assert.h>
#include <limits.h>

namespace {
constexpr time_t kNow = 1000;

NetworkSyncScheduleInput base_input()
{
    NetworkSyncScheduleInput input = {};
    input.now = kNow;
    input.have_weather_key = true;
    input.boot_weather_due_at = kNow + 8;
    input.boot_saying_due_at = kNow + 16;
    return input;
}
} // namespace

int main()
{
    assert(network_boot_budget_remaining_ms(0, 1000) == INT32_MAX);
    assert(network_boot_budget_remaining_ms(-1, 1000) == INT32_MAX);
    assert(network_boot_budget_remaining_ms(1000, 1000) == 0);
    assert(network_boot_budget_remaining_ms(999, 1000) == 0);
    assert(network_boot_budget_remaining_ms(1999, 1000) == 0);
    assert(network_boot_budget_remaining_ms(2000, 1000) == 1);
    assert(network_boot_budget_remaining_ms(6500000, 500000) == 6000);
    assert(network_boot_budget_remaining_ms(static_cast<int64_t>(INT32_MAX) * 1000 + 2000,
                                             0) == INT32_MAX);

    assert(network_idle_wait_ms(kNow, 0, 0) == 300000);
    assert(network_idle_wait_ms(kNow, kNow + 10, 0) == 10000);
    assert(network_idle_wait_ms(kNow, kNow + 10, kNow + 2) == 2000);
    assert(network_idle_wait_ms(kNow, kNow, kNow - 1) == 300000);
    assert(network_idle_wait_ms(kNow, kNow + 600, kNow + 700) == 300000);

    assert(!network_cache_age_is_fresh(kNow, 0, 60));
    assert(!network_cache_age_is_fresh(kNow, kNow - 1, 0));
    assert(!network_cache_age_is_fresh(kNow, kNow + 1, 60));
    assert(network_cache_age_is_fresh(kNow, kNow - 59, 60));
    assert(!network_cache_age_is_fresh(kNow, kNow - 60, 60));

    struct tm local = {};
    local.tm_year = 126;
    local.tm_yday = 193;
    local.tm_hour = 11;
    struct tm cached_local = local;
    cached_local.tm_min = 1;
    assert(network_cache_local_day_matches(local, cached_local));
    assert(network_cache_local_hour_matches(local, cached_local));
    cached_local.tm_hour = 10;
    assert(network_cache_local_day_matches(local, cached_local));
    assert(!network_cache_local_hour_matches(local, cached_local));
    cached_local = local;
    cached_local.tm_yday = 192;
    assert(!network_cache_local_day_matches(local, cached_local));
    assert(!network_cache_local_hour_matches(local, cached_local));
    cached_local = local;
    cached_local.tm_year = 125;
    assert(!network_cache_local_day_matches(local, cached_local));

    NetworkSyncScheduleInput input = base_input();
    input.boot_weather_due = true;
    input.boot_saying_due = true;
    NetworkSyncSchedule schedule = calculate_network_sync_schedule(input);
    assert(!schedule.boot_weather_ready);
    assert(!schedule.boot_saying_ready);
    assert(!schedule.weather_due);
    assert(!schedule.saying_due);
    assert(schedule.next_boot_due_at == kNow + 8);

    input.now = kNow + 8;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.boot_weather_ready);
    assert(!schedule.boot_saying_ready);
    assert(schedule.weather_due);
    assert(!schedule.saying_due);
    assert(schedule.next_boot_due_at == kNow + 16);

    input.now = kNow + 16;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.boot_weather_ready);
    assert(schedule.boot_saying_ready);
    assert(schedule.weather_due);
    assert(schedule.saying_due);
    assert(schedule.next_boot_due_at == 0);

    input = base_input();
    input.provisioning_sync_due = true;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.ntp_due);
    assert(schedule.weather_due);
    assert(schedule.saying_due);

    input.next_ntp_retry_at = kNow + 1;
    schedule = calculate_network_sync_schedule(input);
    assert(!schedule.ntp_due);
    assert(schedule.weather_due);
    assert(schedule.saying_due);

    input = base_input();
    input.manual_ntp_due = true;
    input.manual_weather_due = true;
    input.manual_saying_due = true;
    input.low_battery_mode = true;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.ntp_due);
    assert(!schedule.weather_due);
    assert(!schedule.saying_due);

    input = base_input();
    input.manual_weather_due = true;
    input.manual_saying_due = true;
    input.have_weather_key = false;
    schedule = calculate_network_sync_schedule(input);
    assert(!schedule.weather_due);
    assert(schedule.saying_due);

    input = base_input();
    input.boot_weather_due = true;
    input.boot_saying_due = true;
    input.boot_weather_due_at = kNow + 20;
    input.boot_saying_due_at = kNow + 5;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.next_boot_due_at == kNow + 5);

    input = base_input();
    input.boot_ntp_due = true;
    input.next_ntp_retry_at = kNow;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.ntp_due);

    input = base_input();
    input.midnight_ntp_due = true;
    input.next_ntp_retry_at = kNow - 1;
    schedule = calculate_network_sync_schedule(input);
    assert(schedule.ntp_due);

    return 0;
}
