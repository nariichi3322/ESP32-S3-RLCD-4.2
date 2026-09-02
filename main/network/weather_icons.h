// Provider-neutral weather icon text used by the monochrome RLCD UI.
#pragma once

#include "weather_types.h"

#include <stddef.h>

inline constexpr size_t kWeatherIconTextSize = 2;

struct WeatherIconText {
    char value[kWeatherIconTextSize] = {};
    const char *c_str() const { return value; }
};

WeatherIconText weather_icon_text(WeatherIconKind kind);
