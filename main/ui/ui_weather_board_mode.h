// 天气看板预报卡片的运行期显示模式；不写入持久化设置。
#pragma once

#include <stdint.h>

enum class WeatherBoardForecastMode : uint8_t {
    kDaily,
    kHourly,
};

WeatherBoardForecastMode weather_board_forecast_mode_load();
uint32_t weather_board_forecast_mode_version_load();
void toggle_weather_board_forecast_mode();
