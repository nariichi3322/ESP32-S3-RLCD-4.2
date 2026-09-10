// 验证天气看板日期、温度范围、空气品质与日照格式规则。
#include "ui_weather_board_text.h"
#include "ui_language_internal.h"
#include "ui_i18n.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main()
{
    setenv("TZ", "Asia/Shanghai", 1);
    tzset();

    assert(strcmp(text_or_dash(nullptr), weather_board_dash()) == 0);
    assert(strcmp(text_or_dash(""), weather_board_dash()) == 0);
    assert(strcmp(text_or_dash("晴"), "晴") == 0);

    WeatherForecastDay day = {};
    strcpy(day.date, "2026-07-12");
    strcpy(day.temp_min, "17");
    strcpy(day.temp_max, "26");
    char out[128] = {};
    format_forecast_date_line(day, out, sizeof(out));
    assert(strcmp(out, "星期日\n12日") == 0);
    format_forecast_temp_range(day, out, sizeof(out));
    assert(strcmp(out, "17/26°C") == 0);
    WeatherForecastHour hour = {};
    hour.valid = true;
    strcpy(hour.time, "15:00");
    strcpy(hour.temp, "24");
    format_forecast_hour_time(hour, out, sizeof(out));
    assert(strcmp(out, "15:00") == 0);
    format_forecast_hour_temp(hour, out, sizeof(out));
    assert(strcmp(out, "24°C") == 0);
    format_today_range(day, out, sizeof(out));
    assert(strcmp(out, "今日 17/26°C") == 0);

    strcpy(day.date, "invalid");
    day.temp_min[0] = '\0';
    day.temp_max[0] = '\0';
    format_forecast_date_line(day, out, sizeof(out));
    assert(strcmp(out, "--\n--/--") == 0);
    format_forecast_temp_range(day, out, sizeof(out));
    assert(strcmp(out, "--/--°C") == 0);
    format_today_range(day, out, sizeof(out));
    assert(strcmp(out, ui_text(UiTextId::WeatherTodayPlaceholder)) == 0);

    WeatherAirData air = {};
    format_weather_board_air_line(air, out, sizeof(out));
    assert(strcmp(out, ui_text(UiTextId::WeatherAqiPlaceholder)) == 0);
    air.ready = true;
    strcpy(air.aqi, "42");
    strcpy(air.category, "优");
    format_weather_board_air_line(air, out, sizeof(out));
    assert(strcmp(out, "AQI 42 优") == 0);

    WeatherData weather = {};
    strcpy(weather.humidity, "58");
    format_weather_board_humidity_line(weather, nullptr, out, sizeof(out));
    assert(strcmp(out, "溼度 58%") == 0);
    strcpy(day.humidity, "61");
    format_weather_board_humidity_line(weather, &day, out, sizeof(out));
    assert(strcmp(out, "溼度 61%") == 0);

    format_weather_board_wind_line(nullptr, out, sizeof(out));
    assert(strcmp(out, "-- --級") == 0);
    strcpy(day.wind_dir, "东北风");
    strcpy(day.wind_scale, "3");
    format_weather_board_wind_line(&day, out, sizeof(out));
    assert(strcmp(out, "东北风 3級") == 0);

    format_weather_board_sunrise_line(nullptr, out, sizeof(out));
    assert(strcmp(out, ui_text(UiTextId::WeatherSunrisePlaceholder)) == 0);
    format_weather_board_sunset_line(nullptr, out, sizeof(out));
    assert(strcmp(out, ui_text(UiTextId::WeatherSunsetPlaceholder)) == 0);
    strcpy(day.sunrise, "05:12");
    strcpy(day.sunset, "18:47");
    format_weather_board_sunrise_line(&day, out, sizeof(out));
    assert(strcmp(out, "日出 05:12") == 0);
    format_weather_board_sunset_line(&day, out, sizeof(out));
    assert(strcmp(out, "日落 18:47") == 0);

    WeatherForecastData forecast = {};
    assert(strcmp(weather_board_advice_text(forecast), "等待更多天氣資料") == 0);
    forecast.ready = true;
    strcpy(forecast.advice, "天气平稳，适合轻装出行。");
    assert(strcmp(weather_board_advice_text(forecast), "天氣平穩，適合輕裝出行。") == 0);

    ui_language_store(UiLanguage::Simplified);
    strcpy(day.date, "2026-07-12");
    format_forecast_date_line(day, out, sizeof(out));
    assert(strcmp(out, "星期日\n12日") == 0);
    format_weather_board_humidity_line(weather, &day, out, sizeof(out));
    assert(strcmp(out, "湿度 61%") == 0);

    ui_language_store(UiLanguage::English);
    format_forecast_date_line(day, out, sizeof(out));
    assert(strcmp(out, "Sun\n12") == 0);
    air.aqi_value = 151;
    strcpy(air.aqi, "151");
    format_weather_board_air_line(air, out, sizeof(out));
    assert(strcmp(out, "AQI 151 Unhealthy") == 0);
    day.wind_direction_degrees = 45;
    format_weather_board_wind_line(&day, out, sizeof(out));
    assert(strcmp(out, "NE level 3") == 0);
    format_weather_board_sunrise_line(&day, out, sizeof(out));
    assert(strcmp(out, "Sunrise 05:12") == 0);
    format_weather_board_sunset_line(&day, out, sizeof(out));
    assert(strcmp(out, "Sunset 18:47") == 0);

    format_today_range(day, nullptr, 0);
    format_forecast_date_line(day, nullptr, 0);
    format_forecast_temp_range(day, nullptr, 0);
    format_forecast_hour_time(hour, nullptr, 0);
    format_forecast_hour_temp(hour, nullptr, 0);
    format_weather_board_air_line(air, nullptr, 0);
    format_weather_board_humidity_line(weather, &day, nullptr, 0);
    format_weather_board_wind_line(&day, nullptr, 0);
    format_weather_board_sunrise_line(&day, nullptr, 0);
    format_weather_board_sunset_line(&day, nullptr, 0);
    return 0;
}
