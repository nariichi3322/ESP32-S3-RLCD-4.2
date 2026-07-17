// 定义天气时钟全局运行状态、版本信息和跨模块共享对象。
#include "app_state.h"

const char *const TAG = "WeatherClock";
const char *const APP_VERSION = "v1.5.22";
#ifndef WEATHER_CLOCK_BUILD_DATE
#define WEATHER_CLOCK_BUILD_DATE "unknown"
#endif
const char *const APP_BUILD_DATE = WEATHER_CLOCK_BUILD_DATE;
DisplayPort g_display(12, 11, 5, 40, 41, kDisplayWidth, kDisplayHeight);
I2cMasterBus g_i2c(14, 13, 0);
EventGroupHandle_t g_app_events;
