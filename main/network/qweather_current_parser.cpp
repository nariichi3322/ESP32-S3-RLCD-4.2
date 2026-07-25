// 解析 QWeather 实时天气和空气质量字段，不处理 HTTP 或缓存发布时间。
#include "qweather_current_parser.h"

#include "network_json.h"

#include <string.h>

namespace {
constexpr const char *kWeatherTextField = "text";
constexpr const char *kWeatherIconField = "icon";
constexpr const char *kWeatherTempField = "temp";
constexpr const char *kWeatherHumidityField = "humidity";
constexpr const char *kAirAqiField = "aqi";
constexpr const char *kAirCategoryField = "category";
constexpr const char *kAirPrimaryField = "primary";
constexpr const char *kAirPm25Field = "pm2p5";

} // namespace

bool parse_qweather_current_weather(const cJSON *now, WeatherData *weather)
{
    if (!cJSON_IsObject(now) || !weather) {
        return false;
    }
    const char *text = network_json_object_string_value(now, kWeatherTextField);
    const char *icon = network_json_object_string_value(now, kWeatherIconField);
    const char *temp = network_json_object_string_value(now, kWeatherTempField);
    const char *humidity =
        network_json_object_string_value(now, kWeatherHumidityField);
    if (!text || !icon || !temp || !humidity) {
        return false;
    }
    strlcpy(weather->text, text, sizeof(weather->text));
    strlcpy(weather->icon, icon, sizeof(weather->icon));
    strlcpy(weather->temp, temp, sizeof(weather->temp));
    strlcpy(weather->humidity, humidity, sizeof(weather->humidity));
    return true;
}

bool parse_qweather_current_air(const cJSON *now, WeatherAirData *air)
{
    if (!cJSON_IsObject(now) || !air) {
        return false;
    }
    const char *aqi = network_json_object_string_value(now, kAirAqiField);
    const char *category =
        network_json_object_string_value(now, kAirCategoryField);
    if (!aqi || !category) {
        return false;
    }
    const char *primary =
        network_json_object_string_value(now, kAirPrimaryField);
    const char *pm2p5 = network_json_object_string_value(now, kAirPm25Field);
    strlcpy(air->aqi, aqi, sizeof(air->aqi));
    strlcpy(air->category, category, sizeof(air->category));
    strlcpy(air->primary, primary ? primary : "", sizeof(air->primary));
    strlcpy(air->pm2p5, pm2p5 ? pm2p5 : "", sizeof(air->pm2p5));
    return true;
}
