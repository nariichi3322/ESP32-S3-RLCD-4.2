// 声明手动天气城市的线程安全运行态快照接口。
#pragma once

#include "weather_city_contract.h"

#include <stddef.h>

bool init_manual_weather_city_state();
bool manual_weather_city_snapshot(char *out, size_t out_len);
void manual_weather_city_store(const char *city);
bool manual_weather_city_is_configured();
