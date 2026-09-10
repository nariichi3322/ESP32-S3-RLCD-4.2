// 實作天氣看板不依賴 LVGL 的日期、溫度、空氣品質與日照文字格式。
#include "ui_weather_board_text.h"

#include "app_constexpr.h"
#include "app_time_constants.h"
#include "ui_text_format.h"
#include "ui_language.h"
#include "ui_i18n.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {
constexpr const char *kForecastDateFormat = "%d-%d-%d";
constexpr int kForecastDateFieldCount = 3;
constexpr size_t kForecastShortDateSize = 8;
constexpr int kWeekdayCount = 7;
constexpr UiTextId kShortWeekdayIds[kWeekdayCount] = {
    UiTextId::WeatherBoardWeekdaySundayShort,
    UiTextId::WeatherBoardWeekdayMondayShort,
    UiTextId::WeatherBoardWeekdayTuesdayShort,
    UiTextId::WeatherBoardWeekdayWednesdayShort,
    UiTextId::WeatherBoardWeekdayThursdayShort,
    UiTextId::WeatherBoardWeekdayFridayShort,
    UiTextId::WeatherBoardWeekdaySaturdayShort,
};

const char *weather_board_advice_placeholder()
{
    return ui_text(UiTextId::WeatherWaitingData);
}

bool parse_forecast_date(const char *date, int &year, int &month, int &day)
{
    if (!date) {
        return false;
    }
    return sscanf(date, kForecastDateFormat, &year, &month, &day) ==
           kForecastDateFieldCount;
}

const char *weekday_name_from_date(const char *date)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_forecast_date(date, year, month, day)) {
        return weather_board_dash();
    }
    struct tm tm_value = {};
    tm_value.tm_year = year - kTmYearOffset;
    tm_value.tm_mon = month - kTmMonthOffset;
    tm_value.tm_mday = day;
    tm_value.tm_isdst = -1;
    time_t epoch = mktime(&tm_value);
    if (epoch <= 0) {
        return weather_board_dash();
    }
    localtime_r(&epoch, &tm_value);
    if (tm_value.tm_wday < 0 || tm_value.tm_wday >= kWeekdayCount) {
        return weather_board_dash();
    }
    return ui_text(kShortWeekdayIds[tm_value.tm_wday]);
}

void format_short_date(const char *date, char *out, size_t out_len)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!ui_text_format::output_buffer_available(out, out_len)) {
        return;
    }
    if (!parse_forecast_date(date, year, month, day)) {
        strlcpy(out, ui_text(UiTextId::WeatherBoardShortDatePlaceholder), out_len);
        return;
    }
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherBoardShortDatePlaceholder),
                                ui_format(UiTextId::WeatherDayFormat),
                                day);
}
} // namespace

const char *text_or_dash(const char *text)
{
    return text && text[0] ? text : weather_board_dash();
}

const char *weather_board_waiting_text()
{
    // Keep the board's initial state to one short line. The catalog phrase is
    // intentionally more descriptive for advice/diagnostic contexts, but it
    // cannot fit the 92 px current-condition label.
    return ui_text(UiTextId::WeatherPanelWaiting);
}

const char *weather_board_syncing_text()
{
    return ui_text(UiTextId::WeatherPanelSyncing);
}

void format_today_range(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!ui_text_format::output_buffer_available(out, out_len)) {
        return;
    }
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherTodayPlaceholder),
                                ui_format(UiTextId::WeatherTodayRangeFormat),
                                text_or_dash(day.temp_min),
                                text_or_dash(day.temp_max));
}

void format_forecast_date_line(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!ui_text_format::output_buffer_available(out, out_len)) {
        return;
    }
    char date_short[kForecastShortDateSize] = {};
    format_short_date(day.date, date_short, sizeof(date_short));
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherBoardShortDatePlaceholder),
                                ui_format(UiTextId::WeatherBoardDateLineFormat),
                                weekday_name_from_date(day.date),
                                date_short);
}

void format_forecast_temp_range(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!ui_text_format::output_buffer_available(out, out_len)) {
        return;
    }
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherBoardForecastRangePlaceholder),
                                ui_format(UiTextId::WeatherBoardForecastTempRangeFormat),
                                text_or_dash(day.temp_min),
                                text_or_dash(day.temp_max));
}

void format_forecast_hour_time(const WeatherForecastHour &hour, char *out, size_t out_len)
{
    ui_text_format::copy(out,
                  out_len,
                  hour.valid && hour.time[0]
                      ? hour.time
                      : ui_text(UiTextId::WeatherBoardHourlyTimePlaceholder));
}

void format_forecast_hour_temp(const WeatherForecastHour &hour, char *out, size_t out_len)
{
    if (!hour.valid) {
        ui_text_format::copy(out,
                             out_len,
                             ui_text(UiTextId::WeatherBoardHourlyTempPlaceholder));
        return;
    }
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherBoardHourlyTempPlaceholder),
                                ui_format(UiTextId::WeatherBoardHourlyTempFormat),
                                text_or_dash(hour.temp));
}

void format_weather_board_air_line(const WeatherAirData &air, char *out, size_t out_len)
{
    if (!air.ready) {
        ui_text_format::copy(out, out_len,
                      ui_text(UiTextId::WeatherAqiPlaceholder));
        return;
    }
    const char *category = air.aqi_value >= 0
                               ? ui_air_quality_category(air.aqi_value)
                               : text_or_dash(air.category);
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherAqiPlaceholder),
                                ui_format(UiTextId::WeatherAqiFormat),
                                text_or_dash(air.aqi),
                                category);
}

void format_weather_board_humidity_line(const WeatherData &weather,
                                        const WeatherForecastDay *today,
                                        char *out,
                                        size_t out_len)
{
    const char *humidity = today && today->humidity[0]
                               ? today->humidity
                               : text_or_dash(weather.humidity);
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherHumidityPlaceholder),
                                ui_format(UiTextId::WeatherHumidityFormat),
                                humidity);
}

void format_weather_board_wind_line(const WeatherForecastDay *today,
                                    char *out,
                                    size_t out_len)
{
    const char *direction = weather_board_dash();
    if (today) {
        direction = today->wind_direction_degrees >= 0
                        ? ui_wind_direction(today->wind_direction_degrees)
                        : text_or_dash(today->wind_dir);
    }
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherWindPlaceholder),
                                ui_format(UiTextId::WeatherWindFormat),
                                direction,
                                today ? text_or_dash(today->wind_scale) : weather_board_dash());
}

void format_weather_board_sunrise_line(const WeatherForecastDay *today,
                                       char *out,
                                       size_t out_len)
{
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherSunrisePlaceholder),
                                ui_format(UiTextId::WeatherSunriseFormat),
                                today && today->sunrise[0]
                                    ? today->sunrise
                                    : ui_text(UiTextId::TimePlaceholder));
}

void format_weather_board_sunset_line(const WeatherForecastDay *today,
                                      char *out,
                                      size_t out_len)
{
    ui_text_format::format_or_fallback(out,
                                out_len,
                                ui_text(UiTextId::WeatherSunsetPlaceholder),
                                ui_format(UiTextId::WeatherSunsetFormat),
                                today && today->sunset[0]
                                    ? today->sunset
                                    : ui_text(UiTextId::TimePlaceholder));
}

const char *weather_board_advice_text(const WeatherForecastData &forecast)
{
    if (forecast.ready && forecast.count > 0 && forecast.days[0].valid) {
        return ui_weather_advice(forecast.days[0].weather_code);
    }
    return forecast.ready && forecast.advice[0]
               ? ui_language_localize(forecast.advice)
               : weather_board_advice_placeholder();
}
