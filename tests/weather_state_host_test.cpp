#include "weather_state_internal.h"
#include "app_event_group.h"
#include "app_metadata.h"

#include <assert.h>
#include <atomic>
#include <string.h>

const char *const TAG = "WeatherStateHost";
std::atomic<bool> g_fail_weather_mutex_take{false};
std::atomic<int> g_weather_mutex_create_count{0};
namespace { std::atomic<bool> g_events_ready{false}; }

bool app_event_group_ready() { return g_events_ready.load(); }
EventBits_t app_event_group_set_bits(EventBits_t bits) { return bits; }
EventBits_t app_event_group_clear_bits(EventBits_t) { return 0; }

int main()
{
    assert(init_weather_state());
    WeatherData weather = {};
    strcpy(weather.city, "Taipei");
    WeatherForecastData forecast = {};
    forecast.ready = true;
    forecast.count = 1;
    forecast.days[0].valid = true;
    WeatherAirData air = {};
    air.ready = true;
    commit_weather_update_snapshot(weather, forecast, air, true, true);
    assert(weather_ready_state_load());
    assert(weather_state_version_load() == 1);
    WeatherCacheStatusSnapshot status = {};
    assert(weather_cache_status_snapshot_load(&status));
    assert(status.extended_data_ready);
    clear_weather_ready_event();
    assert(!weather_ready_state_load());
    return 0;
}
