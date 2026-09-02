#pragma once

#include "weather_types.h"

#include <stddef.h>

enum class OpenMeteoResult {
    kOk,
    kInvalidArgument,
    kHttpError,
    kInvalidJson,
    kNotFound,
    kMissingField,
    kInvalidValue,
    kShortArray,
};

OpenMeteoResult parse_open_meteo_geocoding(const char *json,
                                            char *city_name, size_t city_name_len,
                                            char *latitude, size_t latitude_len,
                                            char *longitude, size_t longitude_len);
OpenMeteoResult parse_open_meteo_forecast(const char *json,
                                           const char *latitude,
                                           const char *longitude,
                                           const char *city_name,
                                           WeatherData *weather,
                                           WeatherForecastData *forecast);
OpenMeteoResult parse_open_meteo_air_quality(const char *json,
                                              WeatherAirData *air);
