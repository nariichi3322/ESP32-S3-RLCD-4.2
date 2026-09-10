// 定义应用日志标签、版本号和构建日期等只读元数据。
#include "app_metadata.h"

#ifndef PROJECT_VER
#ifdef ESP_PLATFORM
#error "PROJECT_VER must be provided by the main component CMake configuration"
#else
#define PROJECT_VER "unknown"
#endif
#endif

#ifndef APP_UI_LOCALE
#define APP_UI_LOCALE 0
#endif
#ifndef APP_UI_LOCALE_TAG
#define APP_UI_LOCALE_TAG "zh-TW"
#endif

const char *const TAG = "WeatherClock";
const char *const APP_VERSION = PROJECT_VER;
#ifndef WEATHER_CLOCK_BUILD_DATE
#define WEATHER_CLOCK_BUILD_DATE "unknown"
#endif
const char *const APP_BUILD_DATE = WEATHER_CLOCK_BUILD_DATE;
const char *const APP_LOCALE = APP_UI_LOCALE_TAG;
const uint8_t APP_LOCALE_ID = static_cast<uint8_t>(APP_UI_LOCALE);

__attribute__((used, section(".rodata")))
const WeatherClockAppMetadata APP_IMAGE_METADATA = {
    0x57434C31U, // "WCL1"
    1,
    static_cast<uint8_t>(APP_UI_LOCALE),
    APP_UI_LOCALE_TAG,
};
