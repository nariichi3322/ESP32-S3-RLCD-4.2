// 使用静态任务互斥发布本地温湿度快照，避免普通任务占用中断临界区。
#include "local_sensor_state.h"

#include "scoped_semaphore_lock.h"

#include <atomic>

namespace {
struct LocalSensorSnapshot {
    float temperature = 0.0f;
    float humidity = 0.0f;
    int temperature_trend = 0;
    int humidity_trend = 0;
    uint32_t version = 0;
    bool available = false;
};

StaticTaskMutex s_local_sensor_mutex;
LocalSensorSnapshot s_local_sensor;
std::atomic<uint32_t> s_local_sensor_version{0};
} // namespace

bool init_local_sensor_state()
{
    return s_local_sensor_mutex.init();
}

bool local_sensor_state_publish_sample(float temperature,
                                       float humidity,
                                       int temperature_trend,
                                       int humidity_trend)
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex.handle());
    if (!lock) {
        return false;
    }
    s_local_sensor.temperature = temperature;
    s_local_sensor.humidity = humidity;
    s_local_sensor.temperature_trend = temperature_trend;
    s_local_sensor.humidity_trend = humidity_trend;
    s_local_sensor.available = true;
    ++s_local_sensor.version;
    s_local_sensor_version.store(s_local_sensor.version, std::memory_order_release);
    return true;
}

bool local_sensor_state_publish_unavailable()
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex.handle());
    if (!lock) {
        return false;
    }
    s_local_sensor.available = false;
    ++s_local_sensor.version;
    s_local_sensor_version.store(s_local_sensor.version, std::memory_order_release);
    return true;
}

bool get_local_sensor_snapshot(float *temperature,
                               float *humidity,
                               int *temperature_trend,
                               int *humidity_trend)
{
    ScopedSemaphoreLock lock(s_local_sensor_mutex.handle());
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
    return s_local_sensor_version.load(std::memory_order_acquire);
}
