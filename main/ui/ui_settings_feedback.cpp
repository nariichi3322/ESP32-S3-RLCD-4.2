// 维护设置页反馈文本和手动网络同步状态，不承担设置页绘制。
#include "ui_settings_feedback.h"

#include "app_tick_time.h"
#include "ui_text_format.h"

void notify_ui_task();

namespace {
#define SETTINGS_MANUAL_SYNC_TIMEOUT_LOG_FORMAT "settings manual sync timeout: op=%d"
constexpr const char *kSettingsNtpTimeoutFeedback = "时间同步超时";
constexpr const char *kSettingsWeatherTimeoutFeedback = "天气同步超时";
constexpr const char *kSettingsSayingTimeoutFeedback = "一言更新超时";
} // namespace

void set_settings_feedback(const char *text, uint32_t duration_ms)
{
    ui_text::copy(g_settings_feedback, sizeof(g_settings_feedback), text);
    TickType_t now = xTaskGetTickCount();
    g_settings_feedback_until_tick = now + pdMS_TO_TICKS(duration_ms);
    if (g_settings_requested) {
        g_settings_last_activity_tick = now;
    }
    notify_ui_task();
}

bool is_settings_sync_busy()
{
    return g_settings_sync_op != kSettingsSyncNone || g_network_diag_state == kNetworkDiagRunning;
}

void begin_settings_sync(SettingsSyncOp op, const char *text)
{
    TickType_t now = xTaskGetTickCount();
    g_settings_sync_op = op;
    g_settings_sync_deadline_tick = now + pdMS_TO_TICKS(kSettingsManualSyncTimeoutMs);
    g_settings_last_activity_tick = now;
    set_settings_feedback(text, kSettingsManualSyncTimeoutMs);
}

void finish_settings_sync(SettingsSyncOp op, const char *text)
{
    if (g_settings_sync_op != op) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    g_settings_sync_op = kSettingsSyncNone;
    g_settings_sync_deadline_tick = 0;
    g_settings_last_activity_tick = now;
    set_settings_feedback(text, 3500);
}

bool finish_settings_sync_if_timed_out(TickType_t now)
{
    TickType_t deadline = g_settings_sync_deadline_tick;
    if (!is_settings_sync_busy() || deadline == 0 ||
        !app_tick_deadline_reached(now, deadline)) {
        return false;
    }

    int op = g_settings_sync_op;
    ESP_LOGW(TAG, SETTINGS_MANUAL_SYNC_TIMEOUT_LOG_FORMAT, op);
    if (op == kSettingsSyncNtp) {
        xEventGroupClearBits(g_app_events, kManualNtpSyncBit);
        finish_settings_sync(kSettingsSyncNtp, kSettingsNtpTimeoutFeedback);
    } else if (op == kSettingsSyncWeather) {
        xEventGroupClearBits(g_app_events, kManualWeatherSyncBit);
        finish_settings_sync(kSettingsSyncWeather, kSettingsWeatherTimeoutFeedback);
    } else if (op == kSettingsSyncSaying) {
        xEventGroupClearBits(g_app_events, kManualSayingSyncBit);
        finish_settings_sync(kSettingsSyncSaying, kSettingsSayingTimeoutFeedback);
    } else {
        g_settings_sync_op = kSettingsSyncNone;
        g_settings_sync_deadline_tick = 0;
    }
    return true;
}
