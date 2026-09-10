// 声明天气看板日期、温度范围和预警行的纯文本格式化接口。
#pragma once

#include "weather_types.h"
#include "ui_i18n.h"

#include <stddef.h>

inline const char *weather_board_dash()
{
    return ui_text(UiTextId::UiPlaceholder);
}

const char *text_or_dash(const char *text);
const char *weather_board_waiting_text();
const char *weather_board_syncing_text();
void format_today_range(const WeatherForecastDay &day, char *out, size_t out_len);
void format_forecast_date_line(const WeatherForecastDay &day, char *out, size_t out_len);
void format_forecast_temp_range(const WeatherForecastDay &day, char *out, size_t out_len);
void format_forecast_hour_time(const WeatherForecastHour &hour, char *out, size_t out_len);
void format_forecast_hour_temp(const WeatherForecastHour &hour, char *out, size_t out_len);
void format_weather_board_air_line(const WeatherAirData &air, char *out, size_t out_len);
void format_weather_board_humidity_line(const WeatherData &weather,
                                        const WeatherForecastDay *today,
                                        char *out,
                                        size_t out_len);
void format_weather_board_wind_line(const WeatherForecastDay *today,
                                    char *out,
                                    size_t out_len);
void format_weather_board_sunrise_line(const WeatherForecastDay *today,
                                       char *out,
                                       size_t out_len);
void format_weather_board_sunset_line(const WeatherForecastDay *today,
                                      char *out,
                                      size_t out_len);
const char *weather_board_advice_text(const WeatherForecastData &forecast);
