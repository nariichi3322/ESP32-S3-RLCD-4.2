// Resolves the selected location and publishes Open-Meteo weather snapshots.
#include "weather_update.h"

#include "app_metadata.h"
#include "ascii_text.h"
#include "ip_geolocation_client.h"
#include "manual_weather_city_state.h"
#include "network_credentials_state.h"
#include "network_sync_runtime.h"
#include "open_meteo_client.h"
#include "ui_language.h"
#include "weather_state_internal.h"

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <string.h>

namespace {
constexpr size_t kCityNameSize = sizeof(WeatherData{}.city);
constexpr size_t kWeatherLocationTextSize = 32;
struct WeatherUpdateWorkspace {
    char manual_city[kManualWeatherCityLen] = {};
    char location[kWeatherLocationTextSize] = {};
    char ip_city[kCityNameSize] = {};
    char city[kCityNameSize] = {};
    char latitude[sizeof(WeatherData{}.lat)] = {};
    char longitude[sizeof(WeatherData{}.lon)] = {};
    WeatherData weather = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
};
EXT_RAM_BSS_ATTR WeatherUpdateWorkspace s_workspace;

// Geocoding results barely change; reuse them for a day. Only the network task touches this.
constexpr int64_t kCityCacheTtlUs = 24LL * 60 * 60 * 1000000;
struct CityCache {
    char query[kManualWeatherCityLen];
    char language[8];
    char city[kCityNameSize];
    char latitude[sizeof(WeatherData{}.lat)];
    char longitude[sizeof(WeatherData{}.lon)];
    int64_t expires_us;
};
EXT_RAM_BSS_ATTR CityCache s_city_cache;

bool resolve_city(const char *query, WeatherUpdateWorkspace *workspace)
{
    if (!workspace || !query) return false;
    const char *language = ui_language_open_meteo_tag();
    const int64_t now_us = esp_timer_get_time();
    if (s_city_cache.expires_us > now_us && strcmp(s_city_cache.query, query) == 0 &&
        strcmp(s_city_cache.language, language) == 0) {
        strlcpy(workspace->city, s_city_cache.city, sizeof(workspace->city));
        strlcpy(workspace->latitude, s_city_cache.latitude, sizeof(workspace->latitude));
        strlcpy(workspace->longitude, s_city_cache.longitude, sizeof(workspace->longitude));
        return true;
    }
    if (open_meteo_lookup_city(query, workspace->city, sizeof(workspace->city),
                               workspace->latitude, sizeof(workspace->latitude),
                               workspace->longitude, sizeof(workspace->longitude)) !=
        OpenMeteoResult::kOk) {
        return false;
    }
    strlcpy(s_city_cache.query, query, sizeof(s_city_cache.query));
    strlcpy(s_city_cache.language, language, sizeof(s_city_cache.language));
    strlcpy(s_city_cache.city, workspace->city, sizeof(s_city_cache.city));
    strlcpy(s_city_cache.latitude, workspace->latitude, sizeof(s_city_cache.latitude));
    strlcpy(s_city_cache.longitude, workspace->longitude, sizeof(s_city_cache.longitude));
    s_city_cache.expires_us = now_us + kCityCacheTtlUs;
    return true;
}

WeatherUpdateResult fetch_and_commit(WeatherUpdateWorkspace *workspace)
{
    if (!workspace || !network_sync_continuation_allowed()) return WeatherUpdateResult::kFailed;
    if (open_meteo_fetch_weather(workspace->latitude, workspace->longitude, workspace->city,
                                 &workspace->weather, &workspace->forecast) != OpenMeteoResult::kOk) {
        return WeatherUpdateResult::kFailed;
    }
    const bool air_ok = open_meteo_fetch_air(workspace->latitude, workspace->longitude,
                                             &workspace->air) == OpenMeteoResult::kOk;
    commit_weather_update_snapshot(workspace->weather, workspace->forecast,
                                   workspace->air, true, air_ok);
    return WeatherUpdateResult::kSuccess;
}
} // namespace

WeatherUpdateResult perform_weather_update(WeatherUpdateScope scope)
{
    (void)scope;
    if (!network_weather_configuration_configured()) {
        clear_weather_ready_event();
        return WeatherUpdateResult::kFailed;
    }
    WeatherUpdateWorkspace &workspace = s_workspace;
    memset(&workspace, 0, sizeof(workspace));
    if (manual_weather_city_snapshot(workspace.manual_city, sizeof(workspace.manual_city))) {
        trim_ascii_whitespace(workspace.manual_city);
    }
    if (workspace.manual_city[0] != '\0') {
        if (!resolve_city(workspace.manual_city, &workspace)) {
            ESP_LOGW(TAG, "Open-Meteo city lookup failed: %s", workspace.manual_city);
            return WeatherUpdateResult::kFailed;
        }
        return fetch_and_commit(&workspace);
    }
    if (!ip_geolocation_lookup_cached(workspace.location, sizeof(workspace.location),
                                      workspace.ip_city, sizeof(workspace.ip_city))) {
        ESP_LOGW(TAG, "%s", "IP geolocation lookup failed");
        return WeatherUpdateResult::kFailed;
    }
    const char *comma = strchr(workspace.location, ',');
    if (!comma) return WeatherUpdateResult::kFailed;
    const size_t lon_len = static_cast<size_t>(comma - workspace.location);
    if (lon_len >= sizeof(workspace.longitude)) return WeatherUpdateResult::kFailed;
    memcpy(workspace.longitude, workspace.location, lon_len);
    workspace.longitude[lon_len] = '\0';
    strlcpy(workspace.latitude, comma + 1, sizeof(workspace.latitude));
    strlcpy(workspace.city, workspace.ip_city[0] ? workspace.ip_city : workspace.location,
            sizeof(workspace.city));
    return fetch_and_commit(&workspace);
}
