// 验证天气看板预报模式在运行期切换且不依赖持久化存储。
#include "ui_weather_board_mode.h"

#include <assert.h>

int main()
{
    assert(weather_board_forecast_mode_load() == WeatherBoardForecastMode::kDaily);
    const uint32_t version = weather_board_forecast_mode_version_load();
    toggle_weather_board_forecast_mode();
    assert(weather_board_forecast_mode_load() == WeatherBoardForecastMode::kHourly);
    assert(weather_board_forecast_mode_version_load() == version + 1);
    toggle_weather_board_forecast_mode();
    assert(weather_board_forecast_mode_load() == WeatherBoardForecastMode::kDaily);
    return 0;
}
