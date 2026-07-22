// 声明传感器、电池、小时历史和低频采样调度的公共接口。
#pragma once

#include "sensor_history_types.h"

#include "freertos/FreeRTOS.h"

#include <stdint.h>

class I2cMasterBus;

inline constexpr int kSensorSampleDayMinutes = 1;
inline constexpr int kSensorSampleNightMinutes = 2;

void sample_battery();
bool init_hourly_sensor_history_state();
void reset_hourly_sensor_history();
void load_hourly_sensor_history();
uint32_t get_hourly_sensor_history_version();
bool get_hourly_sensor_history_snapshot(HourlySensorHistoryBlob *history, uint32_t *version);
void init_shtc3_sensor(I2cMasterBus &i2c);
void sample_sensor();
TickType_t next_sensor_sample_tick(TickType_t now);
TickType_t next_battery_sample_tick(TickType_t now);
void housekeeping_task(void *);
