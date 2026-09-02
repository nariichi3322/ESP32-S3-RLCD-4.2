#include "weather_icons.h"

#include <assert.h>
#include <string.h>

int main()
{
    assert(strcmp(weather_icon_text(WeatherIconKind::kClear).c_str(), "O") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kPartlyCloudy).c_str(), "o") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kCloudy).c_str(), "=") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kFog).c_str(), "~") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kDrizzle).c_str(), ",") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kRain).c_str(), "/") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kSnow).c_str(), "*") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kThunderstorm).c_str(), "!") == 0);
    assert(strcmp(weather_icon_text(WeatherIconKind::kUnknown).c_str(), "?") == 0);
    return 0;
}
