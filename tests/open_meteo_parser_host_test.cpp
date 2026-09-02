// Open-Meteo fixture parser coverage: geocoding, weather, forecast, AQI and failures.
#include "open_meteo_parser.h"
#include "open_meteo_client.h"

#include <assert.h>
#include <string.h>

int main()
{
    static_assert(OpenMeteoResult::kHttpError != OpenMeteoResult::kInvalidJson);
    char city[32] = {}, lat[16] = {}, lon[16] = {};
    assert(parse_open_meteo_geocoding(
        R"({"results":[{"name":"臺北","latitude":25.033,"longitude":121.5654}]})",
        city, sizeof(city), lat, sizeof(lat), lon, sizeof(lon)) == OpenMeteoResult::kOk);
    assert(strcmp(city, "臺北") == 0);
    assert(parse_open_meteo_geocoding(R"({"results":[]})", city, sizeof(city),
                                      lat, sizeof(lat), lon, sizeof(lon)) ==
           OpenMeteoResult::kNotFound);
    assert(parse_open_meteo_geocoding("{", city, sizeof(city), lat, sizeof(lat),
                                      lon, sizeof(lon)) == OpenMeteoResult::kInvalidJson);
    assert(parse_open_meteo_geocoding(
        R"({"results":[{"name":"bad","latitude":91,"longitude":0}]})",
        city, sizeof(city), lat, sizeof(lat), lon, sizeof(lon)) ==
           OpenMeteoResult::kInvalidValue);

    const char *forecast_json = R"({
      "current":{"temperature_2m":26.4,"relative_humidity_2m":58,
        "weather_code":2,"wind_speed_10m":12,"wind_direction_10m":45},
      "daily":{
        "time":["2026-09-01","2026-09-02","2026-09-03","2026-09-04","2026-09-05","2026-09-06"],
        "weather_code":[0,2,3,61,71,95],
        "temperature_2m_max":[30,31,29,28,4,27],
        "temperature_2m_min":[22,23,21,20,-2,19],
        "sunrise":["2026-09-01T05:30","2026-09-02T05:31","2026-09-03T05:31","2026-09-04T05:32","2026-09-05T05:32","2026-09-06T05:33"],
        "sunset":["2026-09-01T18:10","2026-09-02T18:09","2026-09-03T18:08","2026-09-04T18:07","2026-09-05T18:06","2026-09-06T18:05"],
        "wind_direction_10m_dominant":[0,45,90,135,180,225],
        "wind_speed_10m_max":[5,12,20,30,40,50]
      }})";
    WeatherData weather = {};
    WeatherForecastData forecast = {};
    assert(parse_open_meteo_forecast(forecast_json, "25.0330", "121.5654",
                                     "臺北", &weather, &forecast) ==
           OpenMeteoResult::kOk);
    assert(forecast.ready && forecast.count == 6 && forecast.days[5].valid);
    assert(weather.icon_kind == WeatherIconKind::kPartlyCloudy);
    assert(forecast.days[3].icon_kind == WeatherIconKind::kRain);
    assert(parse_open_meteo_forecast(
        R"({"current":{},"daily":{}})", "0", "0", "", &weather, &forecast) ==
           OpenMeteoResult::kInvalidValue);
    const char *short_json = R"({
      "current":{"temperature_2m":1,"relative_humidity_2m":2,"weather_code":0,
        "wind_speed_10m":1,"wind_direction_10m":1},
      "daily":{"time":["2026-09-01"],"weather_code":[0],
        "temperature_2m_max":[2],"temperature_2m_min":[1],
        "sunrise":["2026-09-01T05:30"],"sunset":["2026-09-01T18:10"],
        "wind_direction_10m_dominant":[0],"wind_speed_10m_max":[1]}})";
    assert(parse_open_meteo_forecast(short_json, "0", "0", "", &weather, &forecast) ==
           OpenMeteoResult::kShortArray);
    assert(parse_open_meteo_forecast(forecast_json, "91", "0", "", &weather, &forecast) ==
           OpenMeteoResult::kInvalidArgument);

    WeatherAirData air = {};
    assert(parse_open_meteo_air_quality(
        R"({"current":{"us_aqi":42,"pm2_5":8.5,"us_aqi_pm2_5":42,"us_aqi_pm10":20}})",
        &air) == OpenMeteoResult::kOk);
    assert(air.ready && strcmp(air.aqi, "42") == 0);
    assert(parse_open_meteo_air_quality(R"({"current":{"us_aqi":900,"pm2_5":8}})",
                                        &air) == OpenMeteoResult::kInvalidValue);
    return 0;
}
