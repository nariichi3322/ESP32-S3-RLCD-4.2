#include "weather_icons.h"

WeatherIconText weather_icon_text(WeatherIconKind kind)
{
    WeatherIconText result = {{'?', '\0'}};
    switch (kind) {
    case WeatherIconKind::kClear: result.value[0] = 'O'; break;
    case WeatherIconKind::kPartlyCloudy: result.value[0] = 'o'; break;
    case WeatherIconKind::kCloudy: result.value[0] = '='; break;
    case WeatherIconKind::kFog: result.value[0] = '~'; break;
    case WeatherIconKind::kDrizzle: result.value[0] = ','; break;
    case WeatherIconKind::kRain: result.value[0] = '/'; break;
    case WeatherIconKind::kSnow: result.value[0] = '*'; break;
    case WeatherIconKind::kThunderstorm: result.value[0] = '!'; break;
    case WeatherIconKind::kUnknown: break;
    }
    return result;
}
