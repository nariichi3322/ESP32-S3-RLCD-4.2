// 维护设置页反馈文本和手动网络同步状态，不承担设置页绘制。
#include "ui_settings_feedback.h"

#include "app_state.h"
#include "app_event_group.h"
#include "app_tick_time.h"
#include "network_diagnostics_state.h"
#include "scoped_semaphore_lock.h"
#include "ui_settings_activity_state.h"
#include "ui_settings_sync_state.h"
#include "ui_task_notify.h"
#include "ui_text_format.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

namespace {
StaticSemaphore_t s_settings_feedback_mutex_storage = {};
SemaphoreHandle_t s_settings_feedback_mutex = nullptr;
char s_settings_feedback[kSettingsFeedbackTextLen] = {};
TickType_t s_settings_feedback_until_tick = 0;
#define SETTINGS_MANUAL_SYNC_TIMEOUT_LOG_FORMAT "settings manual sync timeout: op=%d"
constexpr const char *kSettingsNtpTimeoutFeedback = "时间同步超时";
constexpr const char *kSettingsWeatherTimeoutFeedback = "天气同步超时";
constexpr const char *kSettingsSayingTimeoutFeedback = "一言更新超时";
static_assert(kSettingsSyncNone == 0, "settings sync state default must mean idle");
} // namespace

bool settings_feedback_state_init()
{
    if (!settings_sync_state_init()) {
        return false;
    }
    if (s_settings_feedback_mutex) {
        return true;
    }
    s_settings_feedback_mutex =
        xSemaphoreCreateMutexStatic(&s_settings_feedback_mutex_storage);
    return s_settings_feedback_mutex != nullptr;
}

void set_settings_feedback(const char *text, uint32_t duration_ms)
{
    char next_feedback[kSettingsFeedbackTextLen] = {};
    ui_text::copy(next_feedback, sizeof(next_feedback), text);
    TickType_t now = xTaskGetTickCount();
    TickType_t until_tick = now + pdMS_TO_TICKS(duration_ms);
    {
        ScopedSemaphoreLock lock(s_settings_feedback_mutex);
        if (!lock) {
            return;
        }
        memcpy(s_settings_feedback, next_feedback, sizeof(s_settings_feedback));
        s_settings_feedback_until_tick = until_tick;
    }
    if (settings_page_requested()) {
        settings_activity_record(now);
    }
    notify_ui_task();
}

void clear_settings_feedback()
{
    ScopedSemaphoreLock lock(s_settings_feedback_mutex);
    if (!lock) {
        return;
    }
    s_settings_feedback[0] = '\0';
    s_settings_feedback_until_tick = 0;
}

bool settings_feedback_copy_active(TickType_t now, char *out, size_t out_len)
{
    if (!ui_text::output_buffer_available(out, out_len)) {
        return false;
    }
    out[0] = '\0';
    char snapshot[kSettingsFeedbackTextLen] = {};
    bool active = false;
    {
        ScopedSemaphoreLock lock(s_settings_feedback_mutex);
        if (!lock) {
            return false;
        }
        active = s_settings_feedback[0] != '\0' &&
                 s_settings_feedback_until_tick != 0 &&
                 app_tick_deadline_pending(now, s_settings_feedback_until_tick);
        if (active) {
            memcpy(snapshot, s_settings_feedback, sizeof(snapshot));
        } else {
            s_settings_feedback[0] = '\0';
            s_settings_feedback_until_tick = 0;
        }
    }
    ui_text::copy(out, out_len, snapshot);
    return active;
}

bool is_settings_sync_busy()
{
    SettingsSyncStateSnapshot state;
    settings_sync_state_load(&state);
    return state.operation != kSettingsSyncNone ||
           network_diag_state_load() == kNetworkDiagRunning;
}

void begin_settings_sync(SettingsSyncOp op, const char *text)
{
    TickType_t now = xTaskGetTickCount();
    settings_sync_state_begin(op, now + pdMS_TO_TICKS(kSettingsManualSyncTimeoutMs));
    settings_activity_record(now);
    set_settings_feedback(text, kSettingsManualSyncTimeoutMs);
}

void finish_settings_sync(SettingsSyncOp op, const char *text)
{
    if (!settings_sync_state_clear_if(op)) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    settings_activity_record(now);
    set_settings_feedback(text, 3500);
}

bool finish_settings_sync_if_timed_out(TickType_t now)
{
    SettingsSyncStateSnapshot state;
    settings_sync_state_load(&state);
    bool busy = state.operation != kSettingsSyncNone ||
                network_diag_state_load() == kNetworkDiagRunning;
    if (!busy || state.deadline_tick == 0 ||
        !app_tick_deadline_reached(now, state.deadline_tick)) {
        return false;
    }

    int op = state.operation;
    ESP_LOGW(TAG, SETTINGS_MANUAL_SYNC_TIMEOUT_LOG_FORMAT, op);
    if (op == kSettingsSyncNtp) {
        app_event_group_clear_bits(kManualNtpSyncBit);
        finish_settings_sync(kSettingsSyncNtp, kSettingsNtpTimeoutFeedback);
    } else if (op == kSettingsSyncWeather) {
        app_event_group_clear_bits(kManualWeatherSyncBit);
        finish_settings_sync(kSettingsSyncWeather, kSettingsWeatherTimeoutFeedback);
    } else if (op == kSettingsSyncSaying) {
        app_event_group_clear_bits(kManualSayingSyncBit);
        finish_settings_sync(kSettingsSyncSaying, kSettingsSayingTimeoutFeedback);
    } else {
        settings_sync_state_clear_if(op);
    }
    return true;
}
