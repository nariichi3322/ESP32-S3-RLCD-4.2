// 声明天气共享快照提交和 ready 事件的内部维护接口。
#pragma once

#include "weather_types.h"

bool init_weather_state();
void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air);
void get_weather_snapshot(WeatherData *weather, WeatherAlertData *alert);
void get_weather_forecast_snapshot(WeatherForecastData *forecast);
void get_weather_air_snapshot(WeatherAirData *air);
time_t get_last_weather_sync_time();
bool weather_extended_data_ready();
void clear_weather_ready_event();
void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherAlertData &next_alert,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok);
