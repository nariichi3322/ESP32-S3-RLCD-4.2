// 声明应用日志标签、版本号和构建日期等只读元数据。
#pragma once

#include <stdint.h>

extern const char *const TAG;
extern const char *const APP_VERSION;
extern const char *const APP_BUILD_DATE;
extern const char *const APP_LOCALE;
extern const uint8_t APP_LOCALE_ID;

// This record is linked into every App image so release tooling can identify
// the locale without relying on the NVS target setting. OTA validators may
// locate it in the image when they perform the optional raw-image check.
struct WeatherClockAppMetadata {
    uint32_t magic;
    uint16_t format_version;
    uint8_t locale_id;
    char locale[8];
};

extern const WeatherClockAppMetadata APP_IMAGE_METADATA;
