// 声明整点提醒、全天提醒、音量和音色的一致原子运行态接口。
#pragma once

#include <stdint.h>

struct ChimeRuntimeSnapshot {
    bool hourly_enabled;
    bool all_day;
    uint8_t volume_percent;
    uint8_t sound_index;
};

ChimeRuntimeSnapshot chime_runtime_snapshot_load();
void chime_runtime_snapshot_store(const ChimeRuntimeSnapshot &snapshot);
bool chime_runtime_hourly_enabled();
bool chime_runtime_all_day_enabled();
bool chime_runtime_any_enabled();
int chime_runtime_volume_percent();
int chime_runtime_sound_index();
void chime_runtime_hourly_enabled_store(bool enabled);
void chime_runtime_all_day_enabled_store(bool enabled);
void chime_runtime_volume_percent_store(int volume_percent);
void chime_runtime_sound_index_store(int sound_index);
