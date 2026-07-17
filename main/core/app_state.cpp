// 定义天气时钟全局运行状态、版本信息和跨模块共享对象。
#include "app_state.h"

const char *const TAG = "WeatherClock";
const char *const APP_VERSION = "v1.5.23";
#ifndef WEATHER_CLOCK_BUILD_DATE
#define WEATHER_CLOCK_BUILD_DATE "unknown"
#endif
const char *const APP_BUILD_DATE = WEATHER_CLOCK_BUILD_DATE;
