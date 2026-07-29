// 仅供天气城市配置持久化所有者发布运行态，普通业务模块保持只读。
#pragma once

#include "manual_weather_city_state.h"

void manual_weather_city_store(const char *city);
