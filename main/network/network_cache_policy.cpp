// 统一后台同步和可见页面使用的天气/每日文字缓存过期判断。
#include "network_cache_policy.h"

#include "sensor_time.h"

namespace {
constexpr time_t kSecondsPerMinute = 60;
constexpr time_t kMinutesPerHour = 60;
constexpr time_t kHoursPerDay = 24;
constexpr time_t kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
constexpr time_t kSecondsPerDay = kHoursPerDay * kSecondsPerHour;

static_assert(kSecondsPerHour == 60 * 60,
              "weather cache fallback must remain one hour");
static_assert(kSecondsPerDay == 24 * 60 * 60,
              "daily saying cache fallback must remain one day");

bool local_cache_times(time_t now,
                       time_t cached_at,
                       struct tm *now_local,
                       struct tm *cached_local)
{
    if (!now_local || !cached_local ||
        !localtime_r(&now, now_local) ||
        !localtime_r(&cached_at, cached_local)) {
        return false;
    }
    return is_tm_plausible(*now_local) &&
           is_tm_plausible(*cached_local);
}

bool local_day_matches(const struct tm &now_local,
                       const struct tm &cached_local)
{
    return now_local.tm_year == cached_local.tm_year &&
           now_local.tm_yday == cached_local.tm_yday;
}
} // namespace

bool network_cache_age_is_fresh(time_t now,
                                time_t cached_at,
                                time_t max_age)
{
    return cached_at > 0 && max_age > 0 && now >= cached_at &&
           now - cached_at < max_age;
}

bool network_weather_cache_current_hour(time_t now, time_t cached_at)
{
    if (cached_at <= 0) {
        return false;
    }
    struct tm now_local = {};
    struct tm cached_local = {};
    if (!local_cache_times(now, cached_at, &now_local, &cached_local)) {
        return network_cache_age_is_fresh(now, cached_at, kSecondsPerHour);
    }
    return local_day_matches(now_local, cached_local) &&
           now_local.tm_hour == cached_local.tm_hour;
}

bool network_daily_saying_cache_current_day(time_t now, time_t cached_at)
{
    if (cached_at <= 0) {
        return false;
    }
    struct tm now_local = {};
    struct tm cached_local = {};
    if (!local_cache_times(now, cached_at, &now_local, &cached_local)) {
        return network_cache_age_is_fresh(now, cached_at, kSecondsPerDay);
    }
    return local_day_matches(now_local, cached_local);
}
