// 直接验证生产天气状态的静态 mutex、事件发布和并发完整快照。
#include "weather_state.h"

#include "app_metadata.h"

#include "app_event_group.h"

#include <assert.h>
#include <atomic>
#include <string.h>
#include <thread>

const char *const TAG = "WeatherStateHost";

std::atomic<bool> g_fail_weather_mutex_take{false};
std::atomic<int> g_weather_mutex_create_count{0};

namespace {
std::atomic<bool> g_events_ready{false};
std::atomic<int> g_event_set_count{0};
std::atomic<int> g_event_clear_count{0};

void fill_snapshot(char marker,
                   WeatherData *weather,
                   WeatherAlertData *alert,
                   WeatherForecastData *forecast,
                   WeatherAirData *air)
{
    const char text[] = {marker, '\0'};
    strlcpy(weather->city, text, sizeof(weather->city));
    strlcpy(weather->text, text, sizeof(weather->text));
    strlcpy(weather->temp, text, sizeof(weather->temp));
    strlcpy(weather->humidity, text, sizeof(weather->humidity));
    strlcpy(weather->icon, text, sizeof(weather->icon));

    alert->active = true;
    alert->count = 1;
    strlcpy(alert->titles[0], text, sizeof(alert->titles[0]));

    forecast->ready = true;
    forecast->count = 1;
    forecast->days[0].valid = true;
    strlcpy(forecast->days[0].date, text, sizeof(forecast->days[0].date));

    air->ready = true;
    strlcpy(air->aqi, text, sizeof(air->aqi));
}

void assert_snapshot_marker(char marker,
                            const WeatherData &weather,
                            const WeatherAlertData &alert,
                            const WeatherForecastData &forecast,
                            const WeatherAirData &air)
{
    assert(weather.city[0] == marker);
    assert(weather.text[0] == marker);
    assert(weather.temp[0] == marker);
    assert(weather.humidity[0] == marker);
    assert(weather.icon[0] == marker);
    assert(alert.active && alert.count == 1);
    assert(alert.titles[0][0] == marker);
    assert(forecast.ready && forecast.count == 1);
    assert(forecast.days[0].valid && forecast.days[0].date[0] == marker);
    assert(air.ready && air.aqi[0] == marker);
}

void commit_marker(char marker)
{
    WeatherData weather = {};
    WeatherAlertData alert = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    fill_snapshot(marker, &weather, &alert, &forecast, &air);
    commit_weather_update_snapshot(weather, alert, forecast, air, true, true);
}
} // namespace

bool app_event_group_ready()
{
    return g_events_ready.load(std::memory_order_acquire);
}

EventBits_t app_event_group_set_bits(EventBits_t bits)
{
    assert(bits == kWeatherReadyBit);
    g_event_set_count.fetch_add(1, std::memory_order_release);
    return bits;
}

EventBits_t app_event_group_clear_bits(EventBits_t bits)
{
    assert(bits == kWeatherReadyBit);
    g_event_clear_count.fetch_add(1, std::memory_order_release);
    return 0;
}

int main()
{
    assert(get_last_weather_sync_time() == 0);
    assert(!weather_extended_data_ready());
    assert(init_weather_state());
    assert(init_weather_state());
    assert(g_weather_mutex_create_count.load() == 1);

    commit_marker('A');
    assert(g_event_set_count.load() == 0);

    g_events_ready.store(true, std::memory_order_release);
    commit_marker('B');
    assert(g_event_set_count.load() == 1);
    assert(get_last_weather_sync_time() > 0);
    assert(weather_extended_data_ready());

    WeatherData weather = {};
    WeatherAlertData alert = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    get_weather_full_snapshot(&weather, &alert, &forecast, &air);
    assert_snapshot_marker('B', weather, alert, forecast, air);

    clear_weather_ready_event();
    assert(g_event_clear_count.load() == 1);

    g_fail_weather_mutex_take.store(true, std::memory_order_release);
    commit_marker('A');
    assert(g_event_set_count.load() == 1);
    assert(get_last_weather_sync_time() == 0);
    g_fail_weather_mutex_take.store(false, std::memory_order_release);
    get_weather_full_snapshot(&weather, &alert, &forecast, &air);
    assert_snapshot_marker('B', weather, alert, forecast, air);

    std::atomic<bool> writer_done{false};
    std::thread writer([&]() {
        for (int i = 0; i < 4000; ++i) {
            commit_marker((i & 1) ? 'A' : 'B');
        }
        writer_done.store(true, std::memory_order_release);
    });
    do {
        get_weather_full_snapshot(&weather, &alert, &forecast, &air);
        assert(weather.city[0] == 'A' || weather.city[0] == 'B');
        assert_snapshot_marker(weather.city[0], weather, alert, forecast, air);
    } while (!writer_done.load(std::memory_order_acquire));
    writer.join();
    return 0;
}
