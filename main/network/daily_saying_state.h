// 管理每日文字及其成功同步时间的任务级一致快照。
#pragma once

#include <stddef.h>
#include <time.h>

bool daily_saying_state_init();
void load_daily_saying_cache();
bool get_daily_saying_snapshot(char *out,
                               size_t out_len,
                               time_t *last_sync_time = nullptr);
bool daily_saying_state_publish(const char *text, time_t synced_at);
