// 使用静态任务互斥发布小智天气城市待保存快照，避免在临界区复制 UTF-8。
#include "weather_city_pending_state.h"

#include "scoped_semaphore_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

namespace {
StaticSemaphore_t s_pending_mutex_storage = {};
SemaphoreHandle_t s_pending_mutex = nullptr;
char s_pending_city[kManualWeatherCityLen] = {};
bool s_pending = false;
uint32_t s_generation = 0;

uint32_t next_generation(uint32_t current)
{
    ++current;
    return current == 0 ? 1 : current;
}
}

bool weather_city_pending_state_init()
{
    if (s_pending_mutex) {
        return true;
    }
    s_pending_mutex = xSemaphoreCreateMutexStatic(&s_pending_mutex_storage);
    return s_pending_mutex != nullptr;
}

bool weather_city_pending_store(const char *city)
{
    char replacement[kManualWeatherCityLen] = {};
    strlcpy(replacement, city ? city : "", sizeof(replacement));

    ScopedSemaphoreLock lock(s_pending_mutex);
    if (!lock) {
        return false;
    }
    memcpy(s_pending_city, replacement, sizeof(s_pending_city));
    s_generation = next_generation(s_generation);
    s_pending = true;
    return true;
}

bool weather_city_pending_snapshot(WeatherCityPendingSnapshot *out)
{
    if (!out) {
        return false;
    }
    *out = {};
    ScopedSemaphoreLock lock(s_pending_mutex);
    if (!lock) {
        return false;
    }
    memcpy(out->city, s_pending_city, sizeof(out->city));
    out->pending = s_pending;
    out->generation = s_generation;
    return true;
}

bool weather_city_pending_exists()
{
    ScopedSemaphoreLock lock(s_pending_mutex);
    return lock && s_pending;
}

bool weather_city_pending_clear(uint32_t generation)
{
    ScopedSemaphoreLock lock(s_pending_mutex);
    if (!lock || !s_pending || generation != s_generation) {
        return false;
    }
    memset(s_pending_city, 0, sizeof(s_pending_city));
    s_pending = false;
    return true;
}
