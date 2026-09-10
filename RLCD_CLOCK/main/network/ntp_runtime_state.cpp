// 使用静态任务互斥维护 NTP 最近成功时间，避免跨任务读取撕裂。
#include "ntp_runtime_state_internal.h"

#include "scoped_semaphore_lock.h"

#include <string.h>

namespace {
StaticTaskMutex s_ntp_state_mutex;
time_t s_last_ntp_sync_time = 0;
char s_ntp_server_name[kNtpServerNameLen] = {};
}

bool ntp_runtime_state_init()
{
    if (!s_ntp_state_mutex.init()) {
        return false;
    }
    ScopedSemaphoreLock lock(s_ntp_state_mutex);
    if (!lock) {
        return false;
    }
    if (s_ntp_server_name[0] == '\0') {
        strlcpy(s_ntp_server_name,
                kDefaultNtpServerName,
                sizeof(s_ntp_server_name));
    }
    return true;
}

time_t ntp_last_sync_time_load()
{
    ScopedSemaphoreLock lock(s_ntp_state_mutex);
    return lock ? s_last_ntp_sync_time : 0;
}

void ntp_last_sync_time_store(time_t sync_time)
{
    ScopedSemaphoreLock lock(s_ntp_state_mutex);
    if (!lock) {
        return;
    }
    s_last_ntp_sync_time = sync_time;
}

bool ntp_server_name_snapshot(char *out, size_t out_len)
{
    if (!out || out_len < sizeof(s_ntp_server_name)) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }
    ScopedSemaphoreLock lock(s_ntp_state_mutex);
    if (!lock) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, s_ntp_server_name, sizeof(s_ntp_server_name));
    return out[0] != '\0';
}

void ntp_server_name_store(const char *server_name)
{
    char normalized[kNtpServerNameLen] = {};
    if (!normalize_ntp_server_name(server_name,
                                   normalized,
                                   sizeof(normalized))) {
        strlcpy(normalized, kDefaultNtpServerName, sizeof(normalized));
    }
    ScopedSemaphoreLock lock(s_ntp_state_mutex);
    if (!lock) {
        return;
    }
    memcpy(s_ntp_server_name, normalized, sizeof(s_ntp_server_name));
}
