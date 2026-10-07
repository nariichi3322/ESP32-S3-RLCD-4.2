// 聚合天气主题纯判断：依据供应商中立的天气图示类别区分晴、雨、雪、多云、阴和雾。
#pragma once
#include "weather_types.h"

inline int aggregate_weather_kind(WeatherIconKind kind) {
    switch (kind) {
    case WeatherIconKind::kClear: return 1;
    case WeatherIconKind::kRain:
    case WeatherIconKind::kDrizzle:
    case WeatherIconKind::kThunderstorm: return 2;
    case WeatherIconKind::kSnow: return 3;
    case WeatherIconKind::kPartlyCloudy: return 4;
    case WeatherIconKind::kCloudy: return 5;
    case WeatherIconKind::kFog: return 6;
    default: return 0;
    }
}
