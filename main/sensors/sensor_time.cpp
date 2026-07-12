// 实现跨页面和后台服务共用的本地时间可信度与整点策略。
#include "sensor_time.h"

#include "app_state.h"

namespace {
constexpr int kSecondsPerHour = 60 * 60;
constexpr int kWeatherSyncFallbackSeconds = kSecondsPerHour;
constexpr int kWeatherSyncSearchHours = 30;
constexpr int kWeatherSyncSearchStepHours = 1;
constexpr int kNightSlowWindowStartHour = 22;
constexpr int kNightSlowWindowEndHour = 6;
constexpr int kTmYearOffset = 1900;

static_assert(kWeatherSyncFallbackSeconds > 0,
              "weather sync fallback interval must be positive");
static_assert(kWeatherSyncSearchHours > 0,
              "weather sync search hours must be positive");
static_assert(kWeatherSyncSearchStepHours > 0,
              "weather sync search step must be positive");
static_assert(kNightSlowWindowStartHour >= 0 && kNightSlowWindowStartHour < 24,
              "night slow window start hour must be in 0..23");
static_assert(kNightSlowWindowEndHour >= 0 && kNightSlowWindowEndHour < 24,
              "night slow window end hour must be in 0..23");
} // namespace

bool is_system_time_plausible(struct tm *local_out)
{
    time_t now;
    time(&now);
    struct tm local = {};
    localtime_r(&now, &local);
    if (local_out) {
        *local_out = local;
    }
    return is_tm_plausible(local);
}

bool is_tm_plausible(const struct tm &local)
{
    int year = local.tm_year + kTmYearOffset;
    return year >= kMinValidYear && year <= kMaxValidYear;
}

bool is_night_slow_window(const struct tm &local)
{
    return local.tm_hour >= kNightSlowWindowStartHour ||
           local.tm_hour < kNightSlowWindowEndHour;
}

int periodic_sample_minutes(const struct tm &local,
                            int day_minutes,
                            int night_minutes)
{
    return is_night_slow_window(local) ? night_minutes : day_minutes;
}

time_t hour_start_from_time(time_t value)
{
    struct tm local = {};
    localtime_r(&value, &local);
    local.tm_min = 0;
    local.tm_sec = 0;
    return mktime(&local);
}

time_t next_weather_sync_time(time_t from)
{
    struct tm candidate = {};
    localtime_r(&from, &candidate);
    if (!is_tm_plausible(candidate)) {
        return from + kWeatherSyncFallbackSeconds;
    }
    candidate.tm_sec = 0;
    candidate.tm_min = 0;
    candidate.tm_hour += kWeatherSyncSearchStepHours;
    time_t next = mktime(&candidate);
    for (int i = 0; i < kWeatherSyncSearchHours; ++i) {
        struct tm local = {};
        localtime_r(&next, &local);
        if (!is_night_slow_window(local) || (local.tm_hour % 2 == 0)) {
            return next;
        }
        local.tm_hour += kWeatherSyncSearchStepHours;
        next = mktime(&local);
    }
    return from + kWeatherSyncFallbackSeconds;
}
