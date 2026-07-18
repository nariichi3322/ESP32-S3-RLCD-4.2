// 使用静态任务互斥发布每日文字和同步时间，避免字符串复制关闭中断。
#include "daily_saying_state.h"

#include "app_text_format.h"
#include "daily_saying_contract.h"
#include "scoped_semaphore_lock.h"

#include <string.h>

namespace {
StaticTaskMutex s_daily_saying_mutex;
char s_daily_saying[kDailySayingLen] = {};
time_t s_last_saying_sync_time = 0;

void prepare_daily_saying(char (&out)[kDailySayingLen], const char *text)
{
    const char *source = text ? text : "";
    const size_t length = strnlen(source, sizeof(out) - 1);
    memcpy(out, source, length);
    out[length] = '\0';
}

void copy_daily_saying_snapshot(char *out,
                                size_t out_len,
                                const char (&source)[kDailySayingLen])
{
    const size_t source_len = strnlen(source, sizeof(source));
    const size_t copy_len = source_len < out_len - 1 ? source_len : out_len - 1;
    memcpy(out, source, copy_len);
    out[copy_len] = '\0';
}
} // namespace

bool daily_saying_state_init()
{
    return s_daily_saying_mutex.init();
}

void load_daily_saying_cache()
{
    (void)daily_saying_state_publish("", 0);
}

bool get_daily_saying_snapshot(char *out,
                               size_t out_len,
                               time_t *last_sync_time)
{
    if (!app_text::output_buffer_available(out, out_len)) {
        return false;
    }

    char text[kDailySayingLen] = {};
    time_t synced_at = 0;
    {
        ScopedSemaphoreLock lock(s_daily_saying_mutex);
        if (!lock) {
            out[0] = '\0';
            if (last_sync_time) {
                *last_sync_time = 0;
            }
            return false;
        }
        memcpy(text, s_daily_saying, sizeof(text));
        synced_at = s_last_saying_sync_time;
    }

    copy_daily_saying_snapshot(out, out_len, text);
    if (last_sync_time) {
        *last_sync_time = synced_at;
    }
    return out[0] != '\0';
}

bool daily_saying_state_publish(const char *text, time_t synced_at)
{
    char replacement[kDailySayingLen] = {};
    prepare_daily_saying(replacement, text);

    ScopedSemaphoreLock lock(s_daily_saying_mutex);
    if (!lock) {
        return false;
    }
    memcpy(s_daily_saying, replacement, sizeof(s_daily_saying));
    s_last_saying_sync_time = synced_at;
    return true;
}
