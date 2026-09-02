#include "weather_snapshot_store.h"

#include <assert.h>
#include <string.h>

int main()
{
    WeatherSnapshotStore store = {};
    WeatherData weather = {};
    strcpy(weather.city, "Taipei");
    strcpy(weather.lat, "25.0330");
    strcpy(weather.lon, "121.5654");
    WeatherForecastData forecast = {};
    forecast.ready = true;
    forecast.count = 1;
    forecast.days[0].valid = true;
    WeatherAirData air = {};
    air.ready = true;
    strcpy(air.aqi, "42");

    weather_snapshot_store_commit(&store, weather, forecast, air, true, true, 100);
    assert(weather_snapshot_store_extended_ready(store));
    assert(store.last_sync_time == 100);

    WeatherData refreshed = weather;
    strcpy(refreshed.temp, "27");
    weather_snapshot_store_commit(&store, refreshed, {}, {}, false, false, 200);
    assert(store.forecast.ready && store.air.ready);

    strcpy(refreshed.city, "Kaohsiung");
    strcpy(refreshed.lat, "22.6273");
    strcpy(refreshed.lon, "120.3014");
    weather_snapshot_store_commit(&store, refreshed, {}, {}, false, false, 300);
    assert(!store.forecast.ready && !store.air.ready);
    return 0;
}
