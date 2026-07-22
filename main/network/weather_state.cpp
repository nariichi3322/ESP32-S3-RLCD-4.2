// 维护天气数据的一致快照、成功更新时间和 ready 事件发布。
#include "weather_state.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "scoped_semaphore_lock.h"
#include "weather_snapshot_store.h"

#include <esp_attr.h>
#include "esp_log.h"

#include <atomic>
#include <string.h>

namespace {
StaticTaskMutex s_weather_state_mutex;
EXT_RAM_BSS_ATTR WeatherSnapshotStore s_weather_store;
constexpr uint32_t kWeatherAlertActiveMask = 1U << 0;
constexpr uint32_t kWeatherAlertCountShift = 1;
constexpr uint32_t kWeatherAlertCountMask = 0x7U << kWeatherAlertCountShift;
constexpr uint32_t kWeatherAlertVersionShift = 4;
constexpr uint32_t kWeatherAlertVersionMask = 0x0fffffffU;
std::atomic<uint32_t> s_weather_alert_status{0};
#define WEATHER_UPDATED_LOG_FORMAT "weather updated: %s %s %sC %s%% icon=%s forecast=%s air=%s"
constexpr const char *kWeatherFetchStatusOk = "ok";
constexpr const char *kWeatherFetchStatusCached = "cached";
constexpr const char *kWeatherReadyEventUnavailableLog =
    "weather ready event skipped: app events unavailable";

static_assert(kMaxWeatherAlerts <= 7,
              "weather alert count must fit the packed status snapshot");
static_assert((kWeatherAlertActiveMask & kWeatherAlertCountMask) == 0,
              "weather alert active and count fields must not overlap");

uint32_t packed_weather_alert_version(uint32_t packed)
{
    return (packed >> kWeatherAlertVersionShift) & kWeatherAlertVersionMask;
}

uint32_t next_weather_alert_version(uint32_t packed)
{
    return (packed_weather_alert_version(packed) + 1U) &
           kWeatherAlertVersionMask;
}

uint32_t pack_weather_alert_status(const WeatherAlertData &alert,
                                   uint32_t version)
{
    int count = alert.count;
    if (count < 0) {
        count = 0;
    } else if (count > kMaxWeatherAlerts) {
        count = kMaxWeatherAlerts;
    }
    return (alert.active ? kWeatherAlertActiveMask : 0U) |
           (static_cast<uint32_t>(count) << kWeatherAlertCountShift) |
           ((version & kWeatherAlertVersionMask) << kWeatherAlertVersionShift);
}

WeatherAlertStatusSnapshot unpack_weather_alert_status(uint32_t packed)
{
    return {
        (packed & kWeatherAlertActiveMask) != 0,
        static_cast<uint8_t>((packed & kWeatherAlertCountMask) >>
                             kWeatherAlertCountShift),
        packed_weather_alert_version(packed),
    };
}

void publish_weather_ready_event()
{
    if (!app_event_group_ready()) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    app_event_group_set_bits(kWeatherReadyBit);
}
} // namespace

bool init_weather_state()
{
    return s_weather_state_mutex.init();
}

void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    if (!lock) {
        return;
    }
    weather_snapshot_store_read(s_weather_store, weather, alert, forecast, air);
}

void get_weather_snapshot(WeatherData *weather)
{
    get_weather_full_snapshot(weather, nullptr, nullptr, nullptr);
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

WeatherAlertStatusSnapshot weather_alert_status_snapshot_load()
{
    return unpack_weather_alert_status(
        s_weather_alert_status.load(std::memory_order_acquire));
}

uint32_t weather_state_version_load()
{
    return packed_weather_alert_version(
        s_weather_alert_status.load(std::memory_order_acquire));
}

bool get_weather_alert_title_snapshot(int requested_index,
                                      char *title,
                                      size_t title_len)
{
    if (!title || title_len == 0) {
        return false;
    }
    title[0] = '\0';
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    if (!lock || !s_weather_store.alert.active ||
        s_weather_store.alert.count <= 0 ||
        s_weather_store.alert.count > kMaxWeatherAlerts) {
        return false;
    }
    int index = requested_index < 0 ? 0 : requested_index;
    index %= s_weather_store.alert.count;
    strlcpy(title, s_weather_store.alert.titles[index], title_len);
    return true;
}

time_t get_last_weather_sync_time()
{
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    return lock ? s_weather_store.last_sync_time : 0;
}

bool weather_extended_data_ready()
{
    ScopedSemaphoreLock lock(s_weather_state_mutex);
    return lock && weather_snapshot_store_extended_ready(s_weather_store);
}

void clear_weather_ready_event()
{
    if (!app_event_group_ready()) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    app_event_group_clear_bits(kWeatherReadyBit);
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
        ScopedSemaphoreLock lock(s_weather_state_mutex);
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
        const uint32_t current =
            s_weather_alert_status.load(std::memory_order_relaxed);
        s_weather_alert_status.store(
            pack_weather_alert_status(next_alert,
                                      next_weather_alert_version(current)),
            std::memory_order_release);
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
