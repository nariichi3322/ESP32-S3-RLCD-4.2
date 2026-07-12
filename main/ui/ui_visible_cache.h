// 声明可见页面天气与每日文字缓存的新鲜度纯判断。
#pragma once

#include <time.h>

bool ui_weather_cache_stale(time_t now_value, time_t last_sync_time);
bool ui_daily_saying_cache_stale(const struct tm &local_value,
                                 time_t now_value,
                                 bool snapshot_ready,
                                 time_t last_sync_time);
