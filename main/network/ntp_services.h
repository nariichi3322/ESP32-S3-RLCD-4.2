// 声明 NTP 同步和最近成功同步时间查询接口。
#pragma once

#include "ntp_server_config.h"

#include <stddef.h>
#include <time.h>

bool perform_ntp_sync(int max_retries = 30);
time_t get_last_ntp_sync_time();
bool get_ntp_server_name(char *out, size_t out_len);
