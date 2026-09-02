#include "open_meteo_parser.h"

#include "open_meteo_client.h"
#include "cJSON.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {
class JsonRoot {
public:
    explicit JsonRoot(const char *json) : value_(json ? cJSON_Parse(json) : nullptr) {}
    ~JsonRoot() { cJSON_Delete(value_); }
    cJSON *get() const { return value_; }
private:
    cJSON *value_;
};

const cJSON *field(const cJSON *object, const char *key)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
}

bool finite_number(const cJSON *object, const char *key, double *out)
{
    const cJSON *item = field(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) return false;
    if (out) *out = item->valuedouble;
    return true;
}

bool ranged_number(const cJSON *object, const char *key,
                   double minimum, double maximum, double *out)
{
    double value = 0;
    if (!finite_number(object, key, &value) || value < minimum || value > maximum) return false;
    if (out) *out = value;
    return true;
}

const cJSON *array_field(const cJSON *object, const char *key)
{
    const cJSON *array = field(object, key);
    return cJSON_IsArray(array) ? array : nullptr;
}

OpenMeteoResult validate_daily(const cJSON *daily)
{
    static constexpr const char *kNumberArrays[] = {
        "weather_code", "temperature_2m_max", "temperature_2m_min",
        "wind_direction_10m_dominant", "wind_speed_10m_max",
    };
    static constexpr const char *kStringArrays[] = {"time", "sunrise", "sunset"};
    for (const char *key : kNumberArrays) {
        const cJSON *array = array_field(daily, key);
        if (!array) return OpenMeteoResult::kMissingField;
        if (cJSON_GetArraySize(array) < kWeatherForecastDays) return OpenMeteoResult::kShortArray;
        for (int i = 0; i < kWeatherForecastDays; ++i) {
            const cJSON *item = cJSON_GetArrayItem(array, i);
            if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble))
                return OpenMeteoResult::kInvalidValue;
            const double value = item->valuedouble;
            if (strcmp(key, "weather_code") == 0 &&
                (value < 0 || value > 99 || std::floor(value) != value))
                return OpenMeteoResult::kInvalidValue;
            if ((strcmp(key, "temperature_2m_max") == 0 ||
                 strcmp(key, "temperature_2m_min") == 0) &&
                (value < -100 || value > 100))
                return OpenMeteoResult::kInvalidValue;
            if (strcmp(key, "wind_direction_10m_dominant") == 0 &&
                (value < 0 || value > 360))
                return OpenMeteoResult::kInvalidValue;
            if (strcmp(key, "wind_speed_10m_max") == 0 &&
                (value < 0 || value > 500))
                return OpenMeteoResult::kInvalidValue;
        }
    }
    for (const char *key : kStringArrays) {
        const cJSON *array = array_field(daily, key);
        if (!array) return OpenMeteoResult::kMissingField;
        if (cJSON_GetArraySize(array) < kWeatherForecastDays) return OpenMeteoResult::kShortArray;
        for (int i = 0; i < kWeatherForecastDays; ++i) {
            const cJSON *item = cJSON_GetArrayItem(array, i);
            if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0])
                return OpenMeteoResult::kInvalidValue;
        }
    }
    return OpenMeteoResult::kOk;
}

double array_number(const cJSON *object, const char *key, int index)
{
    return cJSON_GetArrayItem(array_field(object, key), index)->valuedouble;
}

const char *array_text(const cJSON *object, const char *key, int index)
{
    return cJSON_GetArrayItem(array_field(object, key), index)->valuestring;
}

const char *wmo_text(int code)
{
    if (code == 0) return "晴";
    if (code >= 1 && code <= 2) return "多雲";
    if (code == 3) return "陰";
    if (code == 45 || code == 48) return "霧";
    if (code >= 51 && code <= 57) return "毛毛雨";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "雨";
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "雪";
    if (code >= 95 && code <= 99) return "雷雨";
    return "未知";
}

const char *wind_direction(int degrees)
{
    static constexpr const char *kDirections[] =
        {"北", "東北", "東", "東南", "南", "西南", "西", "西北"};
    return kDirections[((degrees + 22) % 360) / 45];
}

int beaufort_scale(double kmh)
{
    static constexpr double kUpper[] = {1, 5, 11, 19, 28, 38, 49, 61, 74, 88, 102, 117};
    for (int i = 0; i < static_cast<int>(sizeof(kUpper) / sizeof(kUpper[0])); ++i)
        if (kmh <= kUpper[i]) return i;
    return 12;
}

const char *aqi_category(int value)
{
    if (value <= 50) return "優";
    if (value <= 100) return "良";
    if (value <= 150) return "敏感不健康";
    if (value <= 200) return "不健康";
    if (value <= 300) return "非常不健康";
    return "危害";
}

const char *weather_advice(int code)
{
    const WeatherIconKind icon = open_meteo_icon_for_wmo_code(code);
    if (icon == WeatherIconKind::kThunderstorm || icon == WeatherIconKind::kRain ||
        icon == WeatherIconKind::kDrizzle) return "外出請攜帶雨具並留意路況。";
    if (icon == WeatherIconKind::kSnow) return "天氣寒冷，請注意保暖與路面結冰。";
    if (icon == WeatherIconKind::kClear) return "天氣晴朗，戶外活動請注意防曬。";
    if (icon == WeatherIconKind::kFog) return "能見度較低，行車請減速並保持距離。";
    return "天氣可能變化，外出前請留意最新資訊。";
}

void format_number(char *out, size_t out_len, double value, bool decimal = false)
{
    if (out && out_len) snprintf(out, out_len, decimal ? "%.1f" : "%.0f", value);
}

bool coordinate_text_valid(const char *text, double minimum, double maximum)
{
    if (!text || !text[0]) return false;
    char *end = nullptr;
    const double value = strtod(text, &end);
    return end && *end == '\0' && std::isfinite(value) &&
           value >= minimum && value <= maximum;
}
}

OpenMeteoResult parse_open_meteo_geocoding(const char *json,
                                            char *city_name, size_t city_name_len,
                                            char *latitude, size_t latitude_len,
                                            char *longitude, size_t longitude_len)
{
    if (!json || !city_name || !city_name_len || !latitude || !latitude_len ||
        !longitude || !longitude_len) return OpenMeteoResult::kInvalidArgument;
    JsonRoot root(json);
    if (!root.get()) return OpenMeteoResult::kInvalidJson;
    const cJSON *results = field(root.get(), "results");
    if (!cJSON_IsArray(results)) return OpenMeteoResult::kMissingField;
    if (cJSON_GetArraySize(results) == 0) return OpenMeteoResult::kNotFound;
    const cJSON *first = cJSON_GetArrayItem(results, 0);
    const cJSON *name = field(first, "name");
    double lat = 0, lon = 0;
    if (!cJSON_IsString(name) || !name->valuestring || !name->valuestring[0])
        return OpenMeteoResult::kMissingField;
    if (!ranged_number(first, "latitude", -90.0, 90.0, &lat) ||
        !ranged_number(first, "longitude", -180.0, 180.0, &lon))
        return OpenMeteoResult::kInvalidValue;
    const int latitude_written = snprintf(latitude, latitude_len, "%.6f", lat);
    const int longitude_written = snprintf(longitude, longitude_len, "%.6f", lon);
    if (strlcpy(city_name, name->valuestring, city_name_len) >= city_name_len ||
        latitude_written < 0 || static_cast<size_t>(latitude_written) >= latitude_len ||
        longitude_written < 0 || static_cast<size_t>(longitude_written) >= longitude_len)
        return OpenMeteoResult::kInvalidArgument;
    return OpenMeteoResult::kOk;
}

OpenMeteoResult parse_open_meteo_forecast(const char *json,
                                           const char *latitude,
                                           const char *longitude,
                                           const char *city_name,
                                           WeatherData *weather,
                                           WeatherForecastData *forecast)
{
    if (!json || !coordinate_text_valid(latitude, -90.0, 90.0) ||
        !coordinate_text_valid(longitude, -180.0, 180.0) ||
        !weather || !forecast)
        return OpenMeteoResult::kInvalidArgument;
    JsonRoot root(json);
    if (!root.get()) return OpenMeteoResult::kInvalidJson;
    const cJSON *current = field(root.get(), "current");
    const cJSON *daily = field(root.get(), "daily");
    if (!current || !daily) return OpenMeteoResult::kMissingField;
    double temperature = 0, humidity = 0, code_value = 0, wind = 0, direction = 0;
    if (!ranged_number(current, "temperature_2m", -100.0, 100.0, &temperature) ||
        !ranged_number(current, "relative_humidity_2m", 0.0, 100.0, &humidity) ||
        !ranged_number(current, "weather_code", 0.0, 99.0, &code_value) ||
        std::floor(code_value) != code_value ||
        !ranged_number(current, "wind_speed_10m", 0.0, 500.0, &wind) ||
        !ranged_number(current, "wind_direction_10m", 0.0, 360.0, &direction))
        return OpenMeteoResult::kInvalidValue;
    const OpenMeteoResult daily_status = validate_daily(daily);
    if (daily_status != OpenMeteoResult::kOk) return daily_status;

    *weather = WeatherData{};
    strlcpy(weather->city, city_name ? city_name : "", sizeof(weather->city));
    weather->weather_code = static_cast<int>(code_value);
    weather->icon_kind = open_meteo_icon_for_wmo_code(weather->weather_code);
    strlcpy(weather->text, wmo_text(weather->weather_code), sizeof(weather->text));
    format_number(weather->temp, sizeof(weather->temp), temperature);
    format_number(weather->humidity, sizeof(weather->humidity), humidity);
    strlcpy(weather->lat, latitude, sizeof(weather->lat));
    strlcpy(weather->lon, longitude, sizeof(weather->lon));

    *forecast = WeatherForecastData{};
    forecast->ready = true;
    forecast->count = kWeatherForecastDays;
    for (int i = 0; i < kWeatherForecastDays; ++i) {
        WeatherForecastDay &day = forecast->days[i];
        day.valid = true;
        strlcpy(day.date, array_text(daily, "time", i), sizeof(day.date));
        day.weather_code = static_cast<int>(array_number(daily, "weather_code", i));
        day.icon_kind = open_meteo_icon_for_wmo_code(day.weather_code);
        strlcpy(day.text, wmo_text(day.weather_code), sizeof(day.text));
        format_number(day.temp_max, sizeof(day.temp_max), array_number(daily, "temperature_2m_max", i));
        format_number(day.temp_min, sizeof(day.temp_min), array_number(daily, "temperature_2m_min", i));
        strlcpy(day.wind_dir,
                wind_direction(static_cast<int>(array_number(daily, "wind_direction_10m_dominant", i))),
                sizeof(day.wind_dir));
        format_number(day.wind_scale, sizeof(day.wind_scale),
                      beaufort_scale(array_number(daily, "wind_speed_10m_max", i)));
        const char *sunrise = array_text(daily, "sunrise", i);
        const char *sunset = array_text(daily, "sunset", i);
        if (strlen(sunrise) < 16 || strlen(sunset) < 16) return OpenMeteoResult::kInvalidValue;
        strlcpy(day.sunrise, sunrise + 11, sizeof(day.sunrise));
        strlcpy(day.sunset, sunset + 11, sizeof(day.sunset));
    }
    strlcpy(forecast->advice,
            weather_advice(forecast->days[0].weather_code),
            sizeof(forecast->advice));
    return OpenMeteoResult::kOk;
}

OpenMeteoResult parse_open_meteo_air_quality(const char *json, WeatherAirData *air)
{
    if (!json || !air) return OpenMeteoResult::kInvalidArgument;
    JsonRoot root(json);
    if (!root.get()) return OpenMeteoResult::kInvalidJson;
    const cJSON *current = field(root.get(), "current");
    if (!current) return OpenMeteoResult::kMissingField;
    double aqi = 0, pm25 = 0;
    if (!ranged_number(current, "us_aqi", 0.0, 500.0, &aqi) ||
        !ranged_number(current, "pm2_5", 0.0, 2000.0, &pm25))
        return OpenMeteoResult::kInvalidValue;
    static constexpr const char *kKeys[] =
        {"us_aqi_pm2_5", "us_aqi_pm10", "us_aqi_nitrogen_dioxide",
         "us_aqi_ozone", "us_aqi_sulphur_dioxide", "us_aqi_carbon_monoxide"};
    static constexpr const char *kNames[] = {"PM2.5", "PM10", "NO2", "O3", "SO2", "CO"};
    double highest = -1;
    size_t primary = 0;
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
        double value = 0;
        if (finite_number(current, kKeys[i], &value) && value >= 0 && value > highest) {
            highest = value;
            primary = i;
        }
    }
    *air = WeatherAirData{};
    air->ready = true;
    format_number(air->aqi, sizeof(air->aqi), aqi);
    format_number(air->pm2p5, sizeof(air->pm2p5), pm25, true);
    strlcpy(air->category, aqi_category(static_cast<int>(aqi)), sizeof(air->category));
    strlcpy(air->primary, kNames[primary], sizeof(air->primary));
    return OpenMeteoResult::kOk;
}
