// 实现天气快照的完整读取和扩展数据失败保留规则。
#include "weather_snapshot_store.h"

void weather_snapshot_store_read(const WeatherSnapshotStore &store,
                                 WeatherData *weather,
                                 WeatherAlertData *alert,
                                 WeatherForecastData *forecast,
                                 WeatherAirData *air)
{
    if (weather) {
        *weather = store.weather;
    }
    if (alert) {
        *alert = store.alert;
    }
    if (forecast) {
        *forecast = store.forecast;
    }
    if (air) {
        *air = store.air;
    }
}

bool weather_snapshot_store_extended_ready(const WeatherSnapshotStore &store)
{
    return store.forecast.ready &&
           store.forecast.count > 0 &&
           store.forecast.days[0].valid &&
           store.air.ready;
}

void weather_snapshot_store_commit(WeatherSnapshotStore *store,
                                   const WeatherData &next,
                                   const WeatherAlertData &next_alert,
                                   const WeatherForecastData &next_forecast,
                                   const WeatherAirData &next_air,
                                   bool forecast_ok,
                                   bool air_ok,
                                   time_t synced_at)
{
    if (!store) {
        return;
    }
    store->weather = next;
    store->alert = next_alert;
    if (forecast_ok) {
        store->forecast = next_forecast;
    }
    if (air_ok) {
        store->air = next_air;
    }
    store->last_sync_time = synced_at;
}
