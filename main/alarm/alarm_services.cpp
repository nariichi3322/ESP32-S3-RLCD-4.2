// 实现单个、单次有效的本地闹钟；设置入口由小智 MCP 提供。
#include "alarm_services.h"

#include "audio_services.h"
#include "sensor_services.h"
#include "ui_views.h"
#include "xiaozhi_ai.h"
#include "xiaozhi_mcp.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "nvs.h"

namespace {
constexpr const char *kAlarmNvsNamespace = "alarm_v1";
constexpr const char *kAlarmEnabledKey = "enabled";
constexpr const char *kAlarmHourKey = "hour";
constexpr const char *kAlarmMinuteKey = "minute";
constexpr int kAlarmSoundIndex = 1; // 设置页“声音选择 2”。
constexpr int kAlarmHoursPerDay = 24;
constexpr int kAlarmMinutesPerHour = 60;
constexpr uint32_t kAlarmMaximumRingMs = 60U * 1000U;
constexpr uint32_t kAlarmRepeatPauseMs = 5U * 1000U;
constexpr uint32_t kAlarmTaskPollMs = 1000U;
constexpr uint32_t kAlarmAudioReleaseWaitMs = 3000U;
constexpr uint32_t kAlarmAudioReleasePollMs = 20U;
constexpr const char *kAlarmSetResultFormat =
    "{\"enabled\":true,\"hour\":%d,\"minute\":%d,\"single_use\":true}";
constexpr const char *kAlarmDisabledResult =
    "{\"enabled\":false,\"single_use\":true}";

portMUX_TYPE s_alarm_mux = portMUX_INITIALIZER_UNLOCKED;
AlarmSnapshot s_alarm = {false, false, 0, 0, 1};
TaskHandle_t s_alarm_task_handle = nullptr;
std::atomic<bool> s_stop_requested{false};
std::atomic<bool> s_save_pending{false};

bool valid_alarm_time(int hour, int minute)
{
    return hour >= 0 && hour < kAlarmHoursPerDay &&
           minute >= 0 && minute < kAlarmMinutesPerHour;
}

void publish_alarm_state(bool enabled, bool ringing, int hour, int minute)
{
    portENTER_CRITICAL(&s_alarm_mux);
    s_alarm.enabled = enabled;
    s_alarm.ringing = ringing;
    s_alarm.hour = static_cast<uint8_t>(hour);
    s_alarm.minute = static_cast<uint8_t>(minute);
    ++s_alarm.version;
    portEXIT_CRITICAL(&s_alarm_mux);
    if (s_alarm_task_handle) {
        xTaskNotifyGive(s_alarm_task_handle);
    }
    notify_ui_task();
}

bool persist_alarm(bool enabled, int hour, int minute)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kAlarmNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm NVS open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(nvs, kAlarmEnabledKey, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kAlarmHourKey, static_cast<uint8_t>(hour));
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kAlarmMinuteKey, static_cast<uint8_t>(minute));
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm NVS save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool load_alarm()
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kAlarmNvsNamespace, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm NVS load open failed: %s", esp_err_to_name(err));
        return false;
    }
    uint8_t enabled = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    esp_err_t enabled_err = nvs_get_u8(nvs, kAlarmEnabledKey, &enabled);
    esp_err_t hour_err = nvs_get_u8(nvs, kAlarmHourKey, &hour);
    esp_err_t minute_err = nvs_get_u8(nvs, kAlarmMinuteKey, &minute);
    nvs_close(nvs);
    bool missing = enabled_err == ESP_ERR_NVS_NOT_FOUND &&
                   hour_err == ESP_ERR_NVS_NOT_FOUND &&
                   minute_err == ESP_ERR_NVS_NOT_FOUND;
    if (missing) {
        return true;
    }
    if (enabled_err != ESP_OK || hour_err != ESP_OK || minute_err != ESP_OK ||
        enabled > 1 || !valid_alarm_time(hour, minute)) {
        ESP_LOGW(TAG, "alarm NVS state invalid, disabling alarm");
        (void)persist_alarm(false, 0, 0);
        publish_alarm_state(false, false, 0, 0);
        return false;
    }
    publish_alarm_state(enabled != 0, false, hour, minute);
    return true;
}

bool alarm_stop_callback()
{
    return s_stop_requested.load();
}

bool wait_interruptible(uint32_t delay_ms)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t duration = pdMS_TO_TICKS(delay_ms);
    while (xTaskGetTickCount() - started < duration && !s_stop_requested.load()) {
        TickType_t remaining = duration - (xTaskGetTickCount() - started);
        TickType_t slice = remaining > pdMS_TO_TICKS(100) ? pdMS_TO_TICKS(100) : remaining;
        ulTaskNotifyTake(pdTRUE, slice);
    }
    return !s_stop_requested.load();
}

void wait_for_xiaozhi_audio_release()
{
    xiaozhi_ai_set_alarm_suspended(true);
    for (uint32_t waited = 0;
         is_audio_playing() && waited < kAlarmAudioReleaseWaitMs && !s_stop_requested.load();
         waited += kAlarmAudioReleasePollMs) {
        vTaskDelay(pdMS_TO_TICKS(kAlarmAudioReleasePollMs));
    }
}

void run_alarm_ring()
{
    s_stop_requested.store(false);
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    // 先关闭运行态，再释放小智音频后写 NVS；避免实时语音期间 Flash 写入。
    s_save_pending.store(false);
    publish_alarm_state(false, true, snapshot.hour, snapshot.minute);
    wait_for_xiaozhi_audio_release();
    if (!persist_alarm(false, snapshot.hour, snapshot.minute)) {
        ESP_LOGW(TAG, "alarm auto-disable persistence failed");
        s_save_pending.store(true);
    }

    TickType_t started = xTaskGetTickCount();
    TickType_t maximum_duration = pdMS_TO_TICKS(kAlarmMaximumRingMs);
    while (!s_stop_requested.load() && xTaskGetTickCount() - started < maximum_duration) {
        if (play_chime_sound_blocking(kAlarmSoundIndex, alarm_stop_callback)) {
            if (!wait_interruptible(kAlarmRepeatPauseMs)) {
                break;
            }
        } else {
            // 其他短提示音正在占用 Codec 时稍后重试，但总时长仍受 1 分钟限制。
            if (!wait_interruptible(250)) {
                break;
            }
        }
    }
    xiaozhi_ai_set_alarm_suspended(false);
    publish_alarm_state(false, false, snapshot.hour, snapshot.minute);
    if (!alarm_flush_pending_save()) {
        ESP_LOGW(TAG, "alarm deferred auto-disable save failed");
    }
    ESP_LOGI(TAG, "alarm finished stopped=%d", s_stop_requested.load() ? 1 : 0);
}

bool mcp_set_alarm(const XiaozhiMcpAlarmRequest &request, char *result, size_t result_len)
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    if (snapshot.ringing || !valid_alarm_time(request.hour, request.minute)) {
        if (result && result_len > 0) {
            strlcpy(result, "alarm rejected", result_len);
        }
        return false;
    }
    s_stop_requested.store(true);
    publish_alarm_state(true, false, request.hour, request.minute);
    s_save_pending.store(true);
    if (result && result_len > 0) {
        snprintf(result, result_len, kAlarmSetResultFormat, request.hour, request.minute);
    }
    return true;
}

bool mcp_disable_alarm(char *result, size_t result_len)
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    s_stop_requested.store(true);
    publish_alarm_state(false, false, snapshot.hour, snapshot.minute);
    s_save_pending.store(true);
    if (result && result_len > 0) {
        strlcpy(result, kAlarmDisabledResult, result_len);
    }
    return true;
}
} // namespace

void alarm_services_init()
{
    (void)load_alarm();
    xiaozhi_mcp_register_alarm_handler(mcp_set_alarm);
    xiaozhi_mcp_register_alarm_disable_handler(mcp_disable_alarm);
}

void alarm_task(void *)
{
    s_alarm_task_handle = xTaskGetCurrentTaskHandle();
    for (;;) {
        AlarmSnapshot snapshot = {};
        alarm_get_snapshot(&snapshot);
        struct tm local = {};
        if (snapshot.enabled && !snapshot.ringing &&
            is_system_time_plausible(&local) &&
            local.tm_hour == snapshot.hour && local.tm_min == snapshot.minute) {
            run_alarm_ring();
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kAlarmTaskPollMs));
    }
}

void alarm_get_snapshot(AlarmSnapshot *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_alarm_mux);
    *out = s_alarm;
    portEXIT_CRITICAL(&s_alarm_mux);
}

bool alarm_is_enabled()
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    return snapshot.enabled;
}

uint32_t alarm_state_version()
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    return snapshot.version;
}

bool alarm_set_once(int hour, int minute)
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    if (snapshot.ringing || !valid_alarm_time(hour, minute) ||
        !persist_alarm(true, hour, minute)) {
        return false;
    }
    s_save_pending.store(false);
    s_stop_requested.store(true);
    publish_alarm_state(true, false, hour, minute);
    ESP_LOGI(TAG, "alarm set %02d:%02d single-use", hour, minute);
    return true;
}

bool alarm_disable()
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    s_stop_requested.store(true);
    if (!persist_alarm(false, snapshot.hour, snapshot.minute)) {
        return false;
    }
    s_save_pending.store(false);
    publish_alarm_state(false, false, snapshot.hour, snapshot.minute);
    ESP_LOGI(TAG, "alarm disabled");
    return true;
}

bool alarm_stop_ringing_from_button()
{
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    if (!snapshot.ringing) {
        return false;
    }
    s_stop_requested.store(true);
    if (s_alarm_task_handle) {
        xTaskNotifyGive(s_alarm_task_handle);
    }
    return true;
}

bool alarm_clear_saved_state()
{
    s_stop_requested.store(true);
    s_save_pending.store(false);
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kAlarmNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        publish_alarm_state(false, false, 0, 0);
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm NVS clear open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm NVS clear failed: %s", esp_err_to_name(err));
        return false;
    }
    publish_alarm_state(false, false, 0, 0);
    return true;
}

bool alarm_save_pending()
{
    return s_save_pending.load();
}

bool alarm_flush_pending_save()
{
    if (!s_save_pending.load()) {
        return true;
    }
    AlarmSnapshot snapshot = {};
    alarm_get_snapshot(&snapshot);
    if (!persist_alarm(snapshot.enabled, snapshot.hour, snapshot.minute)) {
        return false;
    }
    s_save_pending.store(false);
    return true;
}
