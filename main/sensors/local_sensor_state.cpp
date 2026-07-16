// 使用静态任务互斥发布本地温湿度快照，避免普通任务占用中断临界区。
#include "local_sensor_state.h"

#include "scoped_semaphore_lock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
struct LocalSensorSnapshot {
    float temperature = 0.0f;
    float humidity = 0.0f;
    int temperature_trend = 0;
    int humidity_trend = 0;
    uint32_t version = 0;
    bool available = false;
};

StaticSemaphore_t s_local_sensor_mutex_storage = {};
SemaphoreHandle_t s_local_sensor_mutex = nullptr;
LocalSensorSnapshot s_local_sensor;
} // namespace

bool init_local_sensor_state()
{
    if (s_local_sensor_mutex) {
        return true;
    }
    s_local_sensor_mutex =
        xSemaphoreCreateMutexStatic(&s_local_sensor_mutex_storage);
    return s_local_sensor_mutex != nullptr;
}

bool local_sensor_state_publish_sample(float temperature,
                                       float humidity,
                                       int temperature_trend,
                                       int humidity_trend)
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex);
    if (!lock) {
        return false;
    }
    s_local_sensor.temperature = temperature;
    s_local_sensor.humidity = humidity;
    s_local_sensor.temperature_trend = temperature_trend;
    s_local_sensor.humidity_trend = humidity_trend;
    s_local_sensor.available = true;
    ++s_local_sensor.version;
    return true;
}

bool local_sensor_state_publish_unavailable()
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex);
    if (!lock) {
        return false;
    }
    s_local_sensor.available = false;
    ++s_local_sensor.version;
    return true;
}

bool local_sensor_state_publish_trends(int temperature_trend,
                                       int humidity_trend)
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex);
    if (!lock) {
        return false;
    }
    s_local_sensor.temperature_trend = temperature_trend;
    s_local_sensor.humidity_trend = humidity_trend;
    return true;
}

bool get_local_sensor_snapshot(float *temperature,
                               float *humidity,
                               int *temperature_trend,
                               int *humidity_trend)
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex);
    if (!lock) {
        return false;
    }
    if (temperature) {
        *temperature = s_local_sensor.temperature;
    }
    if (humidity) {
        *humidity = s_local_sensor.humidity;
    }
    if (temperature_trend) {
        *temperature_trend = s_local_sensor.temperature_trend;
    }
    if (humidity_trend) {
        *humidity_trend = s_local_sensor.humidity_trend;
    }
    return s_local_sensor.available;
}

uint32_t local_sensor_state_version()
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex);
    return lock ? s_local_sensor.version : 0;
}
