// 聲明天氣時鐘秒數顯示偏好與執行期唯讀狀態。
#pragma once

#include <stdint.h>

inline constexpr bool kDefaultWeatherClockSecondsVisible = true;

bool weather_clock_seconds_visible_load();
uint32_t weather_clock_seconds_version_load();
