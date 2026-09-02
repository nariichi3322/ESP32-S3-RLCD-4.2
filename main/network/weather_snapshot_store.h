#pragma once
#include "weather_types.h"

struct WeatherSnapshotStore {
    WeatherData weather;
    WeatherForecastData forecast;
    WeatherAirData air;
    time_t last_sync_time = 0;
};

void weather_snapshot_store_read(const WeatherSnapshotStore &store,
                                 WeatherData *weather,
                                 WeatherForecastData *forecast,
                                 WeatherAirData *air);
bool weather_snapshot_store_extended_ready(const WeatherSnapshotStore &store);
void weather_snapshot_store_commit(WeatherSnapshotStore *store,
                                   const WeatherData &next,
                                   const WeatherForecastData &next_forecast,
                                   const WeatherAirData &next_air,
                                   bool forecast_ok,
                                   bool air_ok,
                                   time_t synced_at);
