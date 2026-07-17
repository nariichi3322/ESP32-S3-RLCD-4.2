// 声明传感器、电池、历史数据和电源服务的公共接口。
#pragma once
#include "app_state.h"
#include "local_sensor_state.h"
#include "power_services.h"
#include "sensor_time.h"

inline constexpr int kSensorSampleDayMinutes = 1;
inline constexpr int kSensorSampleNightMinutes = 2;

void restore_system_time_from_rtc();
void sync_rtc_from_system_time();
void release_battery_gauge();
bool init_battery_gauge();
int battery_percent_from_voltage(float voltage);
bool read_battery_percent(int *percent);
void sample_battery();
bool init_hourly_sensor_history_state();
void reset_hourly_sensor_history();
void load_hourly_sensor_history();
void record_hourly_sensor_sample(float temp, float humi);
bool get_hourly_sensor_history_snapshot(HourlySensorHistoryBlob *history, uint32_t *version);
void update_sensor_history(float temp, float humi);
void init_shtc3_sensor(I2cMasterBus &i2c);
void sample_sensor();
TickType_t next_sensor_sample_tick(TickType_t now);
TickType_t next_battery_sample_tick(TickType_t now);
void housekeeping_task(void *);
