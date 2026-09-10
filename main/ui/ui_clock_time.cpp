// 实现天气时钟不依赖 LVGL 的时间状态与整点报时边界规则。
#include "ui_clock_time.h"

#include "app_time_constants.h"
#include "ui_text_format.h"
#include "ui_i18n.h"
#include "ui_language.h"

namespace {
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr int kProgressSegmentCount = 60;
constexpr int kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
constexpr int kSecondsPerDay = kHoursPerDay * kSecondsPerHour;
constexpr int kHourlyChimeMinute = 0;
constexpr int kHourlyChimeLastAcceptedSecond = 2;
constexpr int kWeekdayCount = 7;
static_assert(kProgressSegmentCount > 0, "clock day progress segment count must be positive");
static_assert(kSecondsPerDay > 0, "clock seconds per day must be positive");
static_assert(kHourlyChimeLastAcceptedSecond < kSecondsPerMinute,
              "hourly chime window must fit the first minute");
const char *weekday_name_or_placeholder(int weekday)
{
    if (ui_language_is_english()) {
        return weekday >= 0 && weekday < kWeekdayCount
                   ? ui_text(static_cast<UiTextId>(static_cast<unsigned>(UiTextId::WeatherBoardWeekdaySundayShort) + weekday))
                   : ui_text(UiTextId::ClockDatePlaceholder);
    }
    return weekday >= 0 && weekday < kWeekdayCount
               ? ui_weekday_text(weekday)
               : ui_text(UiTextId::ClockDatePlaceholder);
}
} // namespace

ClockUiTimeSnapshot clock_ui_time_snapshot(const struct tm &local)
{
    ClockUiTimeSnapshot snapshot = {};
    snapshot.minute_key = local.tm_hour * kMinutesPerHour + local.tm_min;
    snapshot.date_key = (local.tm_year + kTmYearOffset) * 10000 +
                        (local.tm_mon + kTmMonthOffset) * 100 +
                        local.tm_mday;
    snapshot.hour_key = local.tm_yday * kHoursPerDay + local.tm_hour;
    int day_seconds = local.tm_hour * kSecondsPerHour +
                      local.tm_min * kSecondsPerMinute +
                      local.tm_sec;
    snapshot.second_key = day_seconds;
    snapshot.day_progress_filled =
        (day_seconds * kProgressSegmentCount) / kSecondsPerDay;
    snapshot.weekday = weekday_name_or_placeholder(local.tm_wday);
    return snapshot;
}

bool clock_hourly_chime_due(const struct tm &local,
                            const ClockUiTimeSnapshot &snapshot,
                            bool chime_enabled,
                            bool low_battery_mode,
                            int last_chime_hour_key)
{
    return chime_enabled &&
           !low_battery_mode &&
           local.tm_min == kHourlyChimeMinute &&
           local.tm_sec <= kHourlyChimeLastAcceptedSecond &&
           snapshot.hour_key != last_chime_hour_key;
}

void format_clock_date_text(char *out,
                            size_t out_len,
                            const struct tm &local,
                            const char *weekday)
{
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::ClockDatePlaceholder),
                                ui_format(UiTextId::ClockDateFormat),
                                local.tm_year + kTmYearOffset,
                                local.tm_mon + kTmMonthOffset,
                                local.tm_mday,
                                weekday ? weekday : ui_text(UiTextId::ClockDatePlaceholder));
}
