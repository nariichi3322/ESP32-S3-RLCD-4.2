// 声明 QWeather 实时天气和空气质量 now 对象的字段解析接口。
#pragma once

#include "weather_types.h"

#include "cJSON.h"

bool parse_qweather_current_weather(const cJSON *now, WeatherData *weather);
bool parse_qweather_current_air(const cJSON *now, WeatherAirData *air);
