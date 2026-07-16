// 验证天气完整快照、扩展数据保留和 ready 判定规则。
#include "weather_snapshot_store.h"

#include <assert.h>
#include <string.h>

namespace {
void fill_initial_snapshot(WeatherData *weather,
                           WeatherAlertData *alert,
                           WeatherForecastData *forecast,
                           WeatherAirData *air)
{
    strlcpy(weather->city, "Hangzhou", sizeof(weather->city));
    strlcpy(weather->temp, "26", sizeof(weather->temp));
    alert->active = true;
    alert->count = 1;
    strlcpy(alert->titles[0], "wind", sizeof(alert->titles[0]));
    forecast->ready = true;
    forecast->count = 1;
    forecast->days[0].valid = true;
    strlcpy(forecast->days[0].date, "2026-07-16", sizeof(forecast->days[0].date));
    air->ready = true;
    strlcpy(air->aqi, "42", sizeof(air->aqi));
}

void test_complete_commit_and_snapshot()
{
    WeatherSnapshotStore store = {};
    WeatherData weather = {};
    WeatherAlertData alert = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    fill_initial_snapshot(&weather, &alert, &forecast, &air);

    weather_snapshot_store_commit(
        &store, weather, alert, forecast, air, true, true, 1234);
    assert(weather_snapshot_store_extended_ready(store));
    assert(store.last_sync_time == 1234);

    WeatherData copied_weather = {};
    WeatherAlertData copied_alert = {};
    WeatherForecastData copied_forecast = {};
    WeatherAirData copied_air = {};
    weather_snapshot_store_read(store,
                                &copied_weather,
                                &copied_alert,
                                &copied_forecast,
                                &copied_air);
    assert(strcmp(copied_weather.city, "Hangzhou") == 0);
    assert(strcmp(copied_weather.temp, "26") == 0);
    assert(copied_alert.active && copied_alert.count == 1);
    assert(strcmp(copied_forecast.days[0].date, "2026-07-16") == 0);
    assert(strcmp(copied_air.aqi, "42") == 0);
}

void test_failed_extended_fetch_preserves_cache()
{
    WeatherSnapshotStore store = {};
    WeatherData weather = {};
    WeatherAlertData alert = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    fill_initial_snapshot(&weather, &alert, &forecast, &air);
    weather_snapshot_store_commit(
        &store, weather, alert, forecast, air, true, true, 100);

    WeatherData replacement = {};
    WeatherAlertData replacement_alert = {};
    WeatherForecastData failed_forecast = {};
    WeatherAirData failed_air = {};
    strlcpy(replacement.city, "Shanghai", sizeof(replacement.city));
    strlcpy(replacement.temp, "30", sizeof(replacement.temp));
    weather_snapshot_store_commit(&store,
                                  replacement,
                                  replacement_alert,
                                  failed_forecast,
                                  failed_air,
                                  false,
                                  false,
                                  200);

    assert(strcmp(store.weather.city, "Shanghai") == 0);
    assert(strcmp(store.weather.temp, "30") == 0);
    assert(!store.alert.active && store.alert.count == 0);
    assert(strcmp(store.forecast.days[0].date, "2026-07-16") == 0);
    assert(strcmp(store.air.aqi, "42") == 0);
    assert(store.last_sync_time == 200);
    assert(weather_snapshot_store_extended_ready(store));
}

void test_partial_extended_readiness()
{
    WeatherSnapshotStore store = {};
    assert(!weather_snapshot_store_extended_ready(store));
    store.forecast.ready = true;
    store.forecast.count = 1;
    assert(!weather_snapshot_store_extended_ready(store));
    store.forecast.days[0].valid = true;
    assert(!weather_snapshot_store_extended_ready(store));
    store.air.ready = true;
    assert(weather_snapshot_store_extended_ready(store));

    weather_snapshot_store_read(store, nullptr, nullptr, nullptr, nullptr);
    weather_snapshot_store_commit(nullptr,
                                  store.weather,
                                  store.alert,
                                  store.forecast,
                                  store.air,
                                  true,
                                  true,
                                  300);
}
} // namespace

int main()
{
    static_assert(sizeof(WeatherSnapshotStore) > 1024,
                  "large weather snapshots must not return to an ISR critical section");
    test_complete_commit_and_snapshot();
    test_failed_extended_fetch_preserves_cache();
    test_partial_extended_readiness();
    return 0;
}
