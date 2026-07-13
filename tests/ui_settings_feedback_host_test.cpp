// 直接验证设置页反馈和手动同步超时收尾的生产实现。
#include "ui_settings_feedback.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

const char *const TAG = "Test";
char g_settings_feedback[48] = {};
TickType_t g_settings_feedback_until_tick = 0;
volatile bool g_settings_requested = false;
volatile TickType_t g_settings_last_activity_tick = 0;
volatile int g_settings_sync_op = kSettingsSyncNone;
volatile TickType_t g_settings_sync_deadline_tick = 0;
volatile int g_network_diag_state = kNetworkDiagIdle;
EventGroupHandle_t g_app_events = reinterpret_cast<EventGroupHandle_t>(1);

namespace {
TickType_t s_now = 0;
EventBits_t s_event_bits = 0;
int s_notify_count = 0;

void reset_state()
{
    memset(g_settings_feedback, 0, sizeof(g_settings_feedback));
    g_settings_feedback_until_tick = 0;
    g_settings_requested = true;
    g_settings_last_activity_tick = 0;
    g_settings_sync_op = kSettingsSyncNone;
    g_settings_sync_deadline_tick = 0;
    g_network_diag_state = kNetworkDiagIdle;
    s_now = 100;
    s_event_bits = kManualNtpSyncBit | kManualWeatherSyncBit | kManualSayingSyncBit;
    s_notify_count = 0;
}

void expect_timeout(SettingsSyncOp op, EventBits_t bit, const char *feedback)
{
    reset_state();
    g_settings_sync_op = op;
    g_settings_sync_deadline_tick = 120;
    assert(!finish_settings_sync_if_timed_out(119));
    assert(g_settings_sync_op == op);
    assert((s_event_bits & bit) != 0);

    s_now = 120;
    assert(finish_settings_sync_if_timed_out(120));
    assert(g_settings_sync_op == kSettingsSyncNone);
    assert(g_settings_sync_deadline_tick == 0);
    assert((s_event_bits & bit) == 0);
    assert(strcmp(g_settings_feedback, feedback) == 0);
    assert(g_settings_last_activity_tick == 120);
    assert(s_notify_count == 1);
}
} // namespace

TickType_t xTaskGetTickCount()
{
    return s_now;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t bits)
{
    EventBits_t previous = s_event_bits;
    s_event_bits &= ~bits;
    return previous;
}

void notify_ui_task()
{
    ++s_notify_count;
}

extern "C" size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size > 0) {
        size_t copy_length = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copy_length);
        dst[copy_length] = '\0';
    }
    return length;
}

int main()
{
    reset_state();
    set_settings_feedback("测试", 2500);
    assert(strcmp(g_settings_feedback, "测试") == 0);
    assert(g_settings_feedback_until_tick == 2600);
    assert(g_settings_last_activity_tick == 100);
    assert(s_notify_count == 1);

    reset_state();
    begin_settings_sync(kSettingsSyncWeather, "同步中");
    assert(g_settings_sync_op == kSettingsSyncWeather);
    assert(g_settings_sync_deadline_tick == 100 + kSettingsManualSyncTimeoutMs);
    assert(strcmp(g_settings_feedback, "同步中") == 0);
    finish_settings_sync(kSettingsSyncNtp, "错误操作");
    assert(g_settings_sync_op == kSettingsSyncWeather);

    expect_timeout(kSettingsSyncNtp, kManualNtpSyncBit, "时间同步超时");
    expect_timeout(kSettingsSyncWeather, kManualWeatherSyncBit, "天气同步超时");
    expect_timeout(kSettingsSyncSaying, kManualSayingSyncBit, "一言更新超时");

    reset_state();
    g_settings_sync_op = kSettingsSyncNtp;
    g_settings_sync_deadline_tick = UINT32_MAX - 4;
    assert(!finish_settings_sync_if_timed_out(UINT32_MAX - 5));
    s_now = 1;
    assert(finish_settings_sync_if_timed_out(1));

    reset_state();
    g_settings_sync_op = kSettingsSyncNetworkDiag;
    g_settings_sync_deadline_tick = 100;
    assert(finish_settings_sync_if_timed_out(100));
    assert(g_settings_sync_op == kSettingsSyncNone);
    assert(g_settings_sync_deadline_tick == 0);
    assert(g_settings_feedback[0] == '\0');
    assert(s_notify_count == 0);
    return 0;
}
