// 维护天气数据的一致快照、成功更新时间和 ready 事件发布。
#include "weather_state.h"

#include "app_state.h"
#include "weather_snapshot_store.h"

#include <freertos/semphr.h>

namespace {
StaticSemaphore_t s_weather_state_mutex_storage = {};
SemaphoreHandle_t s_weather_state_mutex = nullptr;
WeatherSnapshotStore s_weather_store;
#define WEATHER_UPDATED_LOG_FORMAT "weather updated: %s %s %sC %s%% icon=%s forecast=%s air=%s"
constexpr const char *kWeatherFetchStatusOk = "ok";
constexpr const char *kWeatherFetchStatusCached = "cached";
constexpr const char *kWeatherReadyEventUnavailableLog =
    "weather ready event skipped: app events unavailable";

class WeatherStateLockGuard {
public:
    WeatherStateLockGuard()
        : locked_(s_weather_state_mutex &&
                  xSemaphoreTake(s_weather_state_mutex, portMAX_DELAY) == pdTRUE)
    {
    }

    ~WeatherStateLockGuard()
    {
        if (locked_) {
            xSemaphoreGive(s_weather_state_mutex);
        }
    }

    explicit operator bool() const
    {
        return locked_;
    }

    WeatherStateLockGuard(const WeatherStateLockGuard &) = delete;
    WeatherStateLockGuard &operator=(const WeatherStateLockGuard &) = delete;

private:
    bool locked_;
};

void publish_weather_ready_event()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kWeatherReadyBit);
}
} // namespace

bool init_weather_state()
{
    if (s_weather_state_mutex) {
        return true;
    }
    s_weather_state_mutex =
        xSemaphoreCreateMutexStatic(&s_weather_state_mutex_storage);
    return s_weather_state_mutex != nullptr;
}

void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    WeatherStateLockGuard lock;
    if (!lock) {
        return;
    }
    weather_snapshot_store_read(s_weather_store, weather, alert, forecast, air);
}

void get_weather_snapshot(WeatherData *weather, WeatherAlertData *alert)
{
    get_weather_full_snapshot(weather, alert, nullptr, nullptr);
}

void get_weather_forecast_snapshot(WeatherForecastData *forecast)
{
    if (!forecast) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, forecast, nullptr);
}

void get_weather_air_snapshot(WeatherAirData *air)
{
    if (!air) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, nullptr, air);
}

time_t get_last_weather_sync_time()
{
    WeatherStateLockGuard lock;
    return lock ? s_weather_store.last_sync_time : 0;
}

bool weather_extended_data_ready()
{
    WeatherStateLockGuard lock;
    return lock && weather_snapshot_store_extended_ready(s_weather_store);
}

void clear_weather_ready_event()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupClearBits(g_app_events, kWeatherReadyBit);
}

void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherAlertData &next_alert,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok)
{
    time_t now = 0;
    time(&now);
    {
        WeatherStateLockGuard lock;
        if (!lock) {
            return;
        }
        weather_snapshot_store_commit(&s_weather_store,
                                      next,
                                      next_alert,
                                      next_forecast,
                                      next_air,
                                      forecast_ok,
                                      air_ok,
                                      now);
    }
    publish_weather_ready_event();
    ESP_LOGI(TAG, WEATHER_UPDATED_LOG_FORMAT,
             next.city,
             next.text,
             next.temp,
             next.humidity,
             next.icon,
             forecast_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached,
             air_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached);
}
