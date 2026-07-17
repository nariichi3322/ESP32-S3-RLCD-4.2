// 管理温湿度小时历史的任务级一致快照，避免整块数据复制占用中断临界区。
#pragma once

#include "sensor_history_types.h"

bool init_hourly_sensor_history_state();
bool reset_hourly_sensor_history_state();
bool publish_loaded_hourly_sensor_history(const HourlySensorHistoryBlob &history,
                                          int64_t last_saved_at);
bool publish_hourly_sensor_sample(int index,
                                  int64_t last_saved_at,
                                  const HourlySensorSample &sample);
int64_t hourly_sensor_history_last_saved_at();
bool hourly_sensor_history_snapshot(HourlySensorHistoryBlob *history,
                                    uint32_t *version);
