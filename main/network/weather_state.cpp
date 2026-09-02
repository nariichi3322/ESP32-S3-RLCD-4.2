#include "weather_state_internal.h"
#include "app_event_group.h"
#include "app_metadata.h"
#include "scoped_semaphore_lock.h"
#include "weather_snapshot_store.h"
#include <esp_attr.h>
#include <esp_log.h>
#include <atomic>

namespace {
StaticTaskMutex s_weather_state_mutex;
EXT_RAM_BSS_ATTR WeatherSnapshotStore s_weather_store;
std::atomic<uint32_t> s_weather_version{0};
std::atomic<bool> s_weather_ready{false};

void publish_ready()
{
    s_weather_ready.store(true, std::memory_order_release);
    if (app_event_group_ready()) app_event_group_set_bits(kWeatherReadyBit);
}
}

bool init_weather_state() { return s_weather_state_mutex.init(); }

bool get_weather_full_snapshot(WeatherData *weather,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    if (!lock) return false;
    weather_snapshot_store_read(s_weather_store, weather, forecast, air);
    return true;
}

bool get_weather_snapshot(WeatherData *weather)
{
    return get_weather_full_snapshot(weather, nullptr, nullptr);
}

uint32_t weather_state_version_load()
{
    return s_weather_version.load(std::memory_order_acquire);
}

bool weather_ready_state_load()
{
    return s_weather_ready.load(std::memory_order_acquire);
}

bool weather_cache_status_snapshot_load(WeatherCacheStatusSnapshot *out)
{
    if (!out) return false;
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    if (!lock) return false;
    out->last_sync_time = s_weather_store.last_sync_time;
    out->version = s_weather_version.load(std::memory_order_acquire);
    out->extended_data_ready = weather_snapshot_store_extended_ready(s_weather_store);
    return true;
}

void clear_weather_ready_event()
{
    s_weather_ready.store(false, std::memory_order_release);
    if (app_event_group_ready()) app_event_group_clear_bits(kWeatherReadyBit);
}

void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok)
{
    time_t now = 0;
    time(&now);
    {
        ScopedSemaphoreLock lock(s_weather_state_mutex);
        if (!lock) return;
        weather_snapshot_store_commit(&s_weather_store, next, next_forecast,
                                      next_air, forecast_ok, air_ok, now);
        s_weather_version.fetch_add(1, std::memory_order_release);
    }
    publish_ready();
    ESP_LOGI(TAG, "weather updated: %s %s %sC forecast=%d air=%d",
             next.city, next.text, next.temp, forecast_ok, air_ok);
}
