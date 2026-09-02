#pragma once
#include "weather_types.h"
#include <stdint.h>
#include <time.h>

struct WeatherCacheStatusSnapshot {
    time_t last_sync_time = 0;
    uint32_t version = 0;
    bool extended_data_ready = false;
};

bool get_weather_full_snapshot(WeatherData *weather,
                               WeatherForecastData *forecast,
                               WeatherAirData *air);
bool get_weather_snapshot(WeatherData *weather);
uint32_t weather_state_version_load();
bool weather_ready_state_load();
bool weather_cache_status_snapshot_load(WeatherCacheStatusSnapshot *out);
