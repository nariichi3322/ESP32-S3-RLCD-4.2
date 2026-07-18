// 使用静态任务互斥保存手动天气城市，避免跨任务读取半写入 UTF-8。
#include "manual_weather_city_state.h"

#include "scoped_semaphore_lock.h"

#include <string.h>

namespace {
StaticTaskMutex s_manual_weather_city_mutex;
char s_manual_weather_city[kManualWeatherCityLen] = {};
}

bool init_manual_weather_city_state()
{
    return s_manual_weather_city_mutex.init();
}

bool manual_weather_city_snapshot(char *out, size_t out_len)
{
    if (!out || out_len < sizeof(s_manual_weather_city)) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }
    ScopedSemaphoreLock lock(s_manual_weather_city_mutex);
    if (!lock) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, s_manual_weather_city, sizeof(s_manual_weather_city));
    return s_manual_weather_city[0] != '\0';
}

void manual_weather_city_store(const char *city)
{
    char replacement[kManualWeatherCityLen] = {};
    strlcpy(replacement, city ? city : "", sizeof(replacement));
    ScopedSemaphoreLock lock(s_manual_weather_city_mutex);
    if (!lock) {
        return;
    }
    memcpy(s_manual_weather_city, replacement, sizeof(s_manual_weather_city));
}

bool manual_weather_city_is_configured()
{
    ScopedSemaphoreLock lock(s_manual_weather_city_mutex);
    return lock && s_manual_weather_city[0] != '\0';
}
