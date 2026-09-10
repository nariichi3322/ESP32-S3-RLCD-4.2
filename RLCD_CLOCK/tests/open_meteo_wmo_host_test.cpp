#include "open_meteo_client.h"

#include <assert.h>

int main()
{
    assert(open_meteo_icon_for_wmo_code(0) == WeatherIconKind::kClear);
    assert(open_meteo_icon_for_wmo_code(2) == WeatherIconKind::kPartlyCloudy);
    assert(open_meteo_icon_for_wmo_code(3) == WeatherIconKind::kCloudy);
    assert(open_meteo_icon_for_wmo_code(45) == WeatherIconKind::kFog);
    assert(open_meteo_icon_for_wmo_code(53) == WeatherIconKind::kDrizzle);
    assert(open_meteo_icon_for_wmo_code(82) == WeatherIconKind::kRain);
    assert(open_meteo_icon_for_wmo_code(86) == WeatherIconKind::kSnow);
    assert(open_meteo_icon_for_wmo_code(99) == WeatherIconKind::kThunderstorm);
    assert(open_meteo_icon_for_wmo_code(-1) == WeatherIconKind::kUnknown);
    assert(open_meteo_icon_for_wmo_code(100) == WeatherIconKind::kUnknown);
    assert(open_meteo_http_result(true) == OpenMeteoResult::kOk);
    assert(open_meteo_http_result(false) == OpenMeteoResult::kHttpError);
    return 0;
}
