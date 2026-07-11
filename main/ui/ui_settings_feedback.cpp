// 维护设置页反馈文本和手动网络同步状态，不承担设置页绘制。
#include "ui_settings_feedback.h"

#include "ui_text_format.h"
#include "ui_views.h"

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
