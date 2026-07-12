// 计算单次闹钟下一次触发分钟与相对计时结束分钟是否冲突。
#include "reminder_schedule.h"

#include <sys/time.h>
#include <time.h>

namespace {
constexpr int kHoursPerDay = 24;
constexpr int kMinutesPerHour = 60;
constexpr int64_t kMillisecondsPerSecond = 1000;

bool valid_alarm_time(int hour, int minute)
{
    return hour >= 0 && hour < kHoursPerDay &&
           minute >= 0 && minute < kMinutesPerHour;
}

bool same_local_minute(time_t left, time_t right)
{
    struct tm left_local = {};
    struct tm right_local = {};
    localtime_r(&left, &left_local);
    localtime_r(&right, &right_local);
    return left_local.tm_year == right_local.tm_year &&
           left_local.tm_yday == right_local.tm_yday &&
           left_local.tm_hour == right_local.tm_hour &&
           left_local.tm_min == right_local.tm_min;
}

time_t next_alarm_minute(time_t now, int hour, int minute)
{
    struct tm current = {};
    localtime_r(&now, &current);
    struct tm candidate = current;
    candidate.tm_hour = hour;
    candidate.tm_min = minute;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    time_t candidate_time = mktime(&candidate);

    struct tm current_minute = current;
    current_minute.tm_sec = 0;
    current_minute.tm_isdst = -1;
    if (candidate_time < mktime(&current_minute)) {
        candidate.tm_mday += 1;
        candidate_time = mktime(&candidate);
    }
    return candidate_time;
}
} // namespace

bool reminder_targets_same_local_minute(int64_t now_ms,
                                        int alarm_hour,
                                        int alarm_minute,
                                        uint32_t delay_ms)
{
    if (now_ms < 0 || delay_ms == 0 || !valid_alarm_time(alarm_hour, alarm_minute)) {
        return false;
    }
    time_t now = static_cast<time_t>(now_ms / kMillisecondsPerSecond);
    time_t alarm_time = next_alarm_minute(now, alarm_hour, alarm_minute);
    time_t delayed_time = static_cast<time_t>((now_ms + delay_ms) / kMillisecondsPerSecond);
    return same_local_minute(alarm_time, delayed_time);
}

int64_t reminder_wall_clock_ms()
{
    struct timeval now = {};
    gettimeofday(&now, nullptr);
    return static_cast<int64_t>(now.tv_sec) * kMillisecondsPerSecond +
           now.tv_usec / kMillisecondsPerSecond;
}
