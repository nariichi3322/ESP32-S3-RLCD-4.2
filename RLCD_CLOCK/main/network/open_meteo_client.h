// Open-Meteo geocoding, forecast and air-quality provider interface.
#pragma once

#include "open_meteo_parser.h"
#include "weather_types.h"

#include <stddef.h>

constexpr WeatherIconKind open_meteo_icon_for_wmo_code(int code)
{
    if (code == 0) return WeatherIconKind::kClear;
    if (code >= 1 && code <= 2) return WeatherIconKind::kPartlyCloudy;
    if (code == 3) return WeatherIconKind::kCloudy;
    if (code == 45 || code == 48) return WeatherIconKind::kFog;
    if (code >= 51 && code <= 57) return WeatherIconKind::kDrizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WeatherIconKind::kRain;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return WeatherIconKind::kSnow;
    if (code >= 95 && code <= 99) return WeatherIconKind::kThunderstorm;
    return WeatherIconKind::kUnknown;
}

constexpr OpenMeteoResult open_meteo_http_result(bool request_ok)
{
    return request_ok ? OpenMeteoResult::kOk : OpenMeteoResult::kHttpError;
}

OpenMeteoResult open_meteo_lookup_city(const char *location,
                                                 char *city_name,
                                                 size_t city_name_len,
                                                 char *latitude,
                                                 size_t latitude_len,
                                                 char *longitude,
                                                 size_t longitude_len);
OpenMeteoResult open_meteo_fetch_weather(const char *latitude,
                              const char *longitude,
                              const char *city_name,
                              WeatherData *weather,
                              WeatherForecastData *forecast);
OpenMeteoResult open_meteo_fetch_air(const char *latitude,
                          const char *longitude,
                          WeatherAirData *air);
