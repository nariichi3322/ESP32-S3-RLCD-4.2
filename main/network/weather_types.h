// 定義目前天氣、預報和空氣品質的共享值類型。
#pragma once

#include <stdint.h>
#include <time.h>

inline constexpr int kWeatherForecastDays = 6;
inline constexpr int kWeatherAdviceLen = 96;

enum class WeatherIconKind : uint8_t {
    kClear,
    kPartlyCloudy,
    kCloudy,
    kFog,
    kDrizzle,
    kRain,
    kSnow,
    kThunderstorm,
    kUnknown,
};

struct WeatherData {
    int weather_code = -1; // Provider-neutral WMO weather interpretation code.
    char city[32] = {};
    char text[32] = {};
    WeatherIconKind icon_kind = WeatherIconKind::kUnknown;
    char temp[8] = {};
    char humidity[8] = {};
    char lat[16] = {};
    char lon[16] = {};
};

struct WeatherForecastDay {
    bool valid = false;
    int weather_code = -1;
    char date[12] = {};
    char text[24] = {};
    WeatherIconKind icon_kind = WeatherIconKind::kUnknown;
    char temp_max[8] = {};
    char temp_min[8] = {};
    char humidity[8] = {};
    char wind_dir[16] = {};
    char wind_scale[8] = {};
    char sunrise[8] = {};
    char sunset[8] = {};
};

struct WeatherForecastData {
    bool ready = false;
    int count = 0;
    WeatherForecastDay days[kWeatherForecastDays] = {};
    char advice[kWeatherAdviceLen] = {};
    time_t updated_at = 0;
};

struct WeatherAirData {
    bool ready = false;
    char aqi[8] = {};
    char category[16] = {};
    char primary[16] = {};
    char pm2p5[8] = {};
    time_t updated_at = 0;
};
