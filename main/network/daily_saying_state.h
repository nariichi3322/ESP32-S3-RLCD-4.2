// 提供每日文字及其成功同步时间的任务级只读一致快照。
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct DailySayingCacheSnapshot {
    bool available = false;
    time_t last_sync_time = 0;
    uint32_t version = 0;
};

uint32_t daily_saying_state_version_load();
bool daily_saying_cache_snapshot_load(DailySayingCacheSnapshot *out);
bool get_daily_saying_snapshot(char *out,
                               size_t out_len,
                               time_t *last_sync_time = nullptr);
