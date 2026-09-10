#include "ui_weather_board_mode.h"

#include <atomic>

namespace {
std::atomic<WeatherBoardForecastMode> s_weather_board_forecast_mode{
    WeatherBoardForecastMode::kDaily};
std::atomic<uint32_t> s_weather_board_forecast_mode_version{0};
} // namespace

WeatherBoardForecastMode weather_board_forecast_mode_load()
{
    return s_weather_board_forecast_mode.load(std::memory_order_acquire);
}

uint32_t weather_board_forecast_mode_version_load()
{
    return s_weather_board_forecast_mode_version.load(std::memory_order_acquire);
}

void toggle_weather_board_forecast_mode()
{
    const WeatherBoardForecastMode previous = s_weather_board_forecast_mode.load(
        std::memory_order_acquire);
    const WeatherBoardForecastMode next = previous == WeatherBoardForecastMode::kDaily
                                               ? WeatherBoardForecastMode::kHourly
                                               : WeatherBoardForecastMode::kDaily;
    s_weather_board_forecast_mode.store(next, std::memory_order_release);
    s_weather_board_forecast_mode_version.fetch_add(1, std::memory_order_release);
}
