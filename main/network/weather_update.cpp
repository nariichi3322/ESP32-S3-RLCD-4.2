// 负责选择手动城市或 IP 定位，并组合提交完整天气更新结果。
#include "network_services.h"

#include "app_constexpr.h"
#include "qweather_location_text.h"
#include "weather_state.h"

namespace {
constexpr size_t kQweatherCityIdSize = 24;
constexpr size_t kWeatherCityNameSize = 32;
static_assert(kQweatherCityIdSize > 1, "QWeather city id buffer must fit text and NUL");
static_assert(kWeatherCityNameSize <= kManualWeatherCityLen,
              "QWeather city name must fit manual weather city storage");

#define WEATHER_UPDATE_MANUAL_CITY_FORMAT "weather update using manual city: %s"
#define WEATHER_MANUAL_CITY_LOOKUP_FAILED_FORMAT "manual weather city lookup failed: %s"
#define WEATHER_MANUAL_CITY_UPDATE_FAILED_FORMAT "weather update failed for manual city: %s"
#define WEATHER_RETRY_IP_CITY_LOOKUP_FORMAT "retry qweather city lookup by ip city: %s"
#define WEATHER_USING_IP_COORDINATES_FORMAT "using ip coordinates for weather now: %s"
constexpr const char *kWeatherIpLookupUpdateFailedLog = "weather update failed after ip lookup";
constexpr const char *kWeatherIpGeolocationLookupFailedLog = "ip geolocation lookup failed";
constexpr const char *kWeatherUpdateWarningTexts[] = {
    kWeatherIpLookupUpdateFailedLog,
    kWeatherIpGeolocationLookupFailedLog,
};
static_assert(cstr_array_nonempty(kWeatherUpdateWarningTexts),
              "weather update warning texts must be non-empty");

void log_weather_update_warning(const char *message)
{
    ESP_LOGW(TAG, "%s", cstr_nonempty(message) ? message : "weather update failed");
}

bool lookup_weather_city(const char *location,
                         char *city_id,
                         char *city_name,
                         WeatherData *weather)
{
    if (!city_id || !city_name || !weather) {
        return false;
    }
    return qweather_lookup_city(location,
                                city_id,
                                kQweatherCityIdSize,
                                city_name,
                                kWeatherCityNameSize,
                                weather->lat,
                                sizeof(weather->lat),
                                weather->lon,
                                sizeof(weather->lon));
}

bool fetch_and_commit_weather(const char *city_id, WeatherData *next)
{
    if (!city_id || !next || !qweather_fetch_now(city_id, next)) {
        return false;
    }

    WeatherAlertData next_alert = {};
    WeatherForecastData next_forecast = {};
    WeatherAirData next_air = {};
    (void)qweather_fetch_alert(next->lat, next->lon, &next_alert);
    bool forecast_ok = qweather_fetch_daily(city_id, &next_forecast);
    bool air_ok = qweather_fetch_air(city_id, &next_air);
    commit_weather_update_snapshot(*next, next_alert, next_forecast, next_air, forecast_ok, air_ok);
    return true;
}

bool update_weather_by_manual_city(const char *manual_city)
{
    char city_id[kQweatherCityIdSize] = {};
    char lookup_city[kWeatherCityNameSize] = {};
    WeatherData next = {};

    ESP_LOGI(TAG, WEATHER_UPDATE_MANUAL_CITY_FORMAT, manual_city);
    bool have_city_id = lookup_weather_city(manual_city, city_id, lookup_city, &next);
    if (!have_city_id) {
        ESP_LOGW(TAG, WEATHER_MANUAL_CITY_LOOKUP_FAILED_FORMAT, manual_city);
        return false;
    }
    copy_first_nonempty_text(next.city, sizeof(next.city), lookup_city, manual_city);
    if (fetch_and_commit_weather(city_id, &next)) {
        return true;
    }
    ESP_LOGW(TAG, WEATHER_MANUAL_CITY_UPDATE_FAILED_FORMAT, manual_city);
    return false;
}

bool update_weather_by_ip_location()
{
    char location[kWeatherLocationTextSize] = {};
    char city_id[kQweatherCityIdSize] = {};
    char ip_city[kWeatherCityNameSize] = {};
    char lookup_city[kWeatherCityNameSize] = {};
    WeatherData next = {};

    if (!ip_geolocation_lookup(location, sizeof(location), ip_city, sizeof(ip_city))) {
        log_weather_update_warning(kWeatherIpGeolocationLookupFailedLog);
        return false;
    }
    trim_ascii(location);
    bool have_city_id = lookup_weather_city(location, city_id, lookup_city, &next);
    if (!have_city_id && ip_city[0] != '\0') {
        ESP_LOGW(TAG, WEATHER_RETRY_IP_CITY_LOOKUP_FORMAT, ip_city);
        have_city_id = lookup_weather_city(ip_city, city_id, lookup_city, &next);
    }
    copy_first_nonempty_text(next.city, sizeof(next.city), ip_city, lookup_city, location);
    if (!have_city_id) {
        copy_ip_coordinate_location(location, city_id, sizeof(city_id), &next);
        ESP_LOGW(TAG, WEATHER_USING_IP_COORDINATES_FORMAT, city_id);
    }
    if (fetch_and_commit_weather(city_id, &next)) {
        return true;
    }
    log_weather_update_warning(kWeatherIpLookupUpdateFailedLog);
    return false;
}
} // namespace

bool perform_weather_update()
{
    if (!g_have_weather_key || g_low_battery_mode) {
        clear_weather_ready_event();
        return false;
    }

    char manual_city[kManualWeatherCityLen] = {};
    if (g_has_manual_weather_city) {
        strlcpy(manual_city, g_manual_weather_city, sizeof(manual_city));
        trim_ascii(manual_city);
    }
    if (manual_city[0] != '\0') {
        return update_weather_by_manual_city(manual_city);
    }
    return update_weather_by_ip_location();
}
