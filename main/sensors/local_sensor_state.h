// 管理本地温湿度当前值、趋势和版本的任务级一致快照。
#pragma once

#include <stdint.h>

bool init_local_sensor_state();
bool local_sensor_state_publish_sample(float temperature,
                                       float humidity,
                                       int temperature_trend,
                                       int humidity_trend);
bool local_sensor_state_publish_unavailable();
bool get_local_sensor_snapshot(float *temperature,
                               float *humidity,
                               int *temperature_trend,
                               int *humidity_trend);
uint32_t local_sensor_state_version();
