// 维护天气数据的一致快照、成功更新时间和 ready 事件发布。
#include "weather_state.h"

#include "app_constexpr.h"
#include "network_services.h"

namespace {
#define WEATHER_UPDATED_LOG_FORMAT "weather updated: %s %s %sC %s%% icon=%s forecast=%s air=%s"
constexpr const char *kWeatherFetchStatusOk = "ok";
constexpr const char *kWeatherFetchStatusCached = "cached";
constexpr const char *kWeatherReadyEventUnavailableLog =
    "weather ready event skipped: app events unavailable";
constexpr const char *kWeatherStateTexts[] = {
    WEATHER_UPDATED_LOG_FORMAT,
    kWeatherFetchStatusOk,
    kWeatherFetchStatusCached,
    kWeatherReadyEventUnavailableLog,
};

static_assert(array_count(kWeatherStateTexts) > 0,
              "weather state text guard must cover status and logs");
static_assert(cstr_array_nonempty(kWeatherStateTexts),
              "weather state status and log texts must be non-empty");

void publish_weather_ready_event()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kWeatherReadyBit);
}
} // namespace

void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    portENTER_CRITICAL(&g_weather_state_mux);
    if (weather) {
        *weather = g_weather;
    }
    if (alert) {
        *alert = g_weather_alert;
    }
    if (forecast) {
        *forecast = g_weather_forecast;
    }
    if (air) {
        *air = g_weather_air;
    }
    portEXIT_CRITICAL(&g_weather_state_mux);
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

bool weather_extended_data_ready()
{
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    get_weather_full_snapshot(nullptr, nullptr, &forecast, &air);
    return forecast.ready &&
           forecast.count > 0 &&
           forecast.days[0].valid &&
           air.ready;
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
    portENTER_CRITICAL(&g_weather_state_mux);
    g_weather = next;
    g_weather_alert = next_alert;
    if (forecast_ok) {
        g_weather_forecast = next_forecast;
    }
    if (air_ok) {
        g_weather_air = next_air;
    }
    g_last_weather_sync_time = now;
    portEXIT_CRITICAL(&g_weather_state_mux);
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
