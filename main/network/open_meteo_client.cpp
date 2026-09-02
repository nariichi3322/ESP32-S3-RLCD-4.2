// Open-Meteo public endpoint client with bounded per-request PSRAM buffers.
#include "open_meteo_client.h"

#include "network_http_client.h"
#include "network_url.h"
#include "scoped_heap_buffer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr size_t kUrlSize = 512;
constexpr size_t kResponseSize = 16384;
constexpr char kGeocodingUrl[] =
    "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json";
constexpr char kForecastUrl[] =
    "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&timezone=auto&forecast_days=6&temperature_unit=celsius&wind_speed_unit=kmh&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,wind_direction_10m_dominant,wind_speed_10m_max";
constexpr char kAirUrl[] =
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s&current=us_aqi,pm2_5,us_aqi_pm2_5,us_aqi_pm10,us_aqi_nitrogen_dioxide,us_aqi_ozone,us_aqi_sulphur_dioxide,us_aqi_carbon_monoxide";

bool coordinate_valid(const char *text, double minimum, double maximum)
{
    if (!text || !text[0]) return false;
    char *end = nullptr;
    const double value = strtod(text, &end);
    return end && *end == '\0' && std::isfinite(value) && value >= minimum && value <= maximum;
}

bool request(const char *url, ScopedHeapBuffer<char> *response)
{
    return url && response && *response &&
           http_get_text(url, response->data(), response->size()) == ESP_OK;
}
}

OpenMeteoResult open_meteo_lookup_city(const char *location,
                                        char *city_name, size_t city_name_len,
                                        char *latitude, size_t latitude_len,
                                        char *longitude, size_t longitude_len)
{
    if (!location || !location[0]) return OpenMeteoResult::kInvalidArgument;
    char encoded[256] = {};
    char url[kUrlSize] = {};
    if (!url_encode_component(location, encoded, sizeof(encoded)))
        return OpenMeteoResult::kInvalidArgument;
    const int written = snprintf(url, sizeof(url), kGeocodingUrl, encoded);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(url))
        return OpenMeteoResult::kInvalidArgument;
    ScopedHeapBuffer<char> response(kResponseSize, HeapBufferInit::kZeroed,
                                    HeapBufferStorage::kPsramRequired);
    if (open_meteo_http_result(request(url, &response)) != OpenMeteoResult::kOk)
        return OpenMeteoResult::kHttpError;
    return parse_open_meteo_geocoding(response.data(), city_name, city_name_len,
                                      latitude, latitude_len, longitude, longitude_len);
}

OpenMeteoResult open_meteo_fetch_weather(const char *latitude,
                                          const char *longitude,
                                          const char *city_name,
                                          WeatherData *weather,
                                          WeatherForecastData *forecast)
{
    if (!coordinate_valid(latitude, -90.0, 90.0) ||
        !coordinate_valid(longitude, -180.0, 180.0) || !weather || !forecast)
        return OpenMeteoResult::kInvalidArgument;
    char url[kUrlSize] = {};
    const int written = snprintf(url, sizeof(url), kForecastUrl, latitude, longitude);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(url))
        return OpenMeteoResult::kInvalidArgument;
    ScopedHeapBuffer<char> response(kResponseSize, HeapBufferInit::kZeroed,
                                    HeapBufferStorage::kPsramRequired);
    if (open_meteo_http_result(request(url, &response)) != OpenMeteoResult::kOk)
        return OpenMeteoResult::kHttpError;
    return parse_open_meteo_forecast(response.data(), latitude, longitude,
                                     city_name, weather, forecast);
}

OpenMeteoResult open_meteo_fetch_air(const char *latitude,
                                      const char *longitude,
                                      WeatherAirData *air)
{
    if (!coordinate_valid(latitude, -90.0, 90.0) ||
        !coordinate_valid(longitude, -180.0, 180.0) || !air)
        return OpenMeteoResult::kInvalidArgument;
    char url[kUrlSize] = {};
    const int written = snprintf(url, sizeof(url), kAirUrl, latitude, longitude);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(url))
        return OpenMeteoResult::kInvalidArgument;
    ScopedHeapBuffer<char> response(kResponseSize, HeapBufferInit::kZeroed,
                                    HeapBufferStorage::kPsramRequired);
    if (open_meteo_http_result(request(url, &response)) != OpenMeteoResult::kOk)
        return OpenMeteoResult::kHttpError;
    return parse_open_meteo_air_quality(response.data(), air);
}
