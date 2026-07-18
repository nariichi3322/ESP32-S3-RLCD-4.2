// 声明天气整点与每日文字自然日缓存共用的新鲜度规则。
#pragma once

#include <time.h>

bool network_cache_age_is_fresh(time_t now,
                                time_t cached_at,
                                time_t max_age);
bool network_weather_cache_current_hour(time_t now, time_t cached_at);
bool network_daily_saying_cache_current_day(time_t now, time_t cached_at);
