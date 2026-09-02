#pragma once
#include "weather_state.h"

bool init_weather_state();
void clear_weather_ready_event();
void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok);
