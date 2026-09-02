#include "weather_snapshot_store.h"
#include <string.h>

namespace {
bool has_text(const char *text) { return text && text[0] != '\0'; }
bool same_location(const WeatherData &a, const WeatherData &b)
{
    if (has_text(a.lat) || has_text(b.lat)) {
        return has_text(a.lat) && has_text(a.lon) && has_text(b.lat) && has_text(b.lon) &&
               strcmp(a.lat, b.lat) == 0 && strcmp(a.lon, b.lon) == 0;
    }
    return has_text(a.city) && has_text(b.city) && strcmp(a.city, b.city) == 0;
}
}

void weather_snapshot_store_read(const WeatherSnapshotStore &store,
                                 WeatherData *weather,
                                 WeatherForecastData *forecast,
                                 WeatherAirData *air)
{
    if (weather) *weather = store.weather;
    if (forecast) *forecast = store.forecast;
    if (air) *air = store.air;
}

bool weather_snapshot_store_extended_ready(const WeatherSnapshotStore &store)
{
    return store.forecast.ready && store.forecast.count > 0 &&
           store.forecast.days[0].valid && store.air.ready;
}

void weather_snapshot_store_commit(WeatherSnapshotStore *store,
                                   const WeatherData &next,
                                   const WeatherForecastData &next_forecast,
                                   const WeatherAirData &next_air,
                                   bool forecast_ok,
                                   bool air_ok,
                                   time_t synced_at)
{
    if (!store) return;
    const bool location_unchanged = same_location(store->weather, next);
    store->weather = next;
    if (forecast_ok) store->forecast = next_forecast;
    else if (!location_unchanged) store->forecast = WeatherForecastData{};
    if (air_ok) store->air = next_air;
    else if (!location_unchanged) store->air = WeatherAirData{};
    store->last_sync_time = synced_at;
}
