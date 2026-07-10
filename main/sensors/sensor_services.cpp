// 调度本地温湿度、电池采样和传感器相关低频后台任务。
#include "sensor_services.h"

#include "ota_services.h"
#include "ui_views.h"

namespace {
constexpr uint32_t kHousekeepingOtaPauseDelayMs = 5000;
constexpr uint32_t kHousekeepingFallbackDelayMs = 1000;
constexpr TickType_t kHousekeepingOtaPauseDelay = pdMS_TO_TICKS(kHousekeepingOtaPauseDelayMs);
constexpr TickType_t kHousekeepingFallbackDelay = pdMS_TO_TICKS(kHousekeepingFallbackDelayMs);
constexpr TickType_t kBatteryChargingSampleDelay = pdMS_TO_TICKS(kBatteryChargingSampleMs);
static_assert(kHousekeepingOtaPauseDelayMs > 0, "housekeeping OTA pause delay must be positive");
static_assert(kHousekeepingFallbackDelayMs > 0, "housekeeping fallback delay must be positive");
static_assert(kBatteryChargingSampleMs > 0, "battery charging sample delay must be positive");
static_assert(kBatteryChargeProbeSampleMs > 0, "battery charge probe delay must be positive");
static_assert(kBatteryChargingSampleMs < kBatteryChargeProbeSampleMs,
              "confirmed charging samples should be faster than charge probes");
static_assert(kHousekeepingOtaPauseDelayMs >= kHousekeepingFallbackDelayMs,
              "housekeeping OTA pause delay should not be shorter than fallback delay");
static_assert(kHousekeepingOtaPauseDelay > 0, "housekeeping OTA pause tick delay must be positive");
static_assert(kHousekeepingFallbackDelay > 0, "housekeeping fallback tick delay must be positive");
static_assert(kBatteryChargingSampleDelay > 0, "battery charging sample tick delay must be positive");

TickType_t next_housekeeping_wake_tick(bool low_battery, TickType_t next_sensor, TickType_t next_battery)
{
    if (low_battery) {
        return next_battery;
    }
    return next_sensor < next_battery ? next_sensor : next_battery;
}

TickType_t next_battery_wake_after_sample(TickType_t sampled_tick)
{
    if (g_battery_charging && !g_battery_animation_complete) {
        return sampled_tick + kBatteryChargingSampleDelay;
    }
    return next_battery_sample_tick(sampled_tick);
}

TickType_t delay_until_housekeeping_wake(TickType_t next_wake)
{
    TickType_t now = xTaskGetTickCount();
    return next_wake > now ? next_wake - now : kHousekeepingFallbackDelay;
}

bool system_time_became_valid(bool current_valid, bool previous_valid)
{
    return current_valid && !previous_valid;
}
} // namespace

void housekeeping_task(void *)
{
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t next_sensor = next_sensor_sample_tick(start_tick);
    TickType_t next_battery = next_battery_sample_tick(start_tick);
    bool last_time_valid = is_system_time_plausible();
    if (!g_low_battery_mode) {
        sample_sensor();
    }
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (ota_flow_active()) {
            vTaskDelay(kHousekeepingOtaPauseDelay);
            next_sensor = next_sensor_sample_tick(xTaskGetTickCount());
            next_battery = next_battery_sample_tick(xTaskGetTickCount());
            continue;
        }
        bool time_valid = is_system_time_plausible();
        if (system_time_became_valid(time_valid, last_time_valid)) {
            next_sensor = next_sensor_sample_tick(now);
            next_battery = next_battery_sample_tick(now);
        }
        last_time_valid = time_valid;
        if (now >= next_sensor) {
            if (!g_low_battery_mode) {
                sample_sensor();
            }
            next_sensor = next_sensor_sample_tick(xTaskGetTickCount());
        }
        if (now >= next_battery) {
            bool was_low_battery = g_low_battery_mode;
            sample_battery();
            TickType_t after_battery = xTaskGetTickCount();
            if (was_low_battery && !g_low_battery_mode) {
                next_sensor = next_sensor_sample_tick(after_battery);
            }
            next_battery = next_battery_wake_after_sample(after_battery);
        }
        TickType_t next_wake = next_housekeeping_wake_tick(g_low_battery_mode, next_sensor, next_battery);
        vTaskDelay(delay_until_housekeeping_wake(next_wake));
    }
}
