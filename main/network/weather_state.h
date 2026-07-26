// 声明天气共享快照提交和 ready 事件的内部维护接口。
#pragma once

#include "weather_types.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct WeatherAlertStatusSnapshot {
    bool active = false;
    uint8_t count = 0;
    uint32_t version = 0;
};

struct WeatherCacheStatusSnapshot {
    time_t last_sync_time = 0;
    uint32_t version = 0;
    bool extended_data_ready = false;
};

bool init_weather_state();
void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air);
void get_weather_snapshot(WeatherData *weather);
void get_weather_forecast_snapshot(WeatherForecastData *forecast);
void get_weather_air_snapshot(WeatherAirData *air);
WeatherAlertStatusSnapshot weather_alert_status_snapshot_load();
uint32_t weather_state_version_load();
bool weather_ready_state_load();
bool weather_cache_status_snapshot_load(WeatherCacheStatusSnapshot *out);
bool get_weather_alert_title_snapshot(int requested_index,
                                      char *title,
                                      size_t title_len);
void clear_weather_ready_event();
void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherAlertData &next_alert,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok);
void commit_weather_resource_deferred_snapshot(
    const WeatherData &next,
    const WeatherAlertData &next_alert,
    const WeatherForecastData &next_forecast,
    bool alert_updated,
    bool forecast_ok);
