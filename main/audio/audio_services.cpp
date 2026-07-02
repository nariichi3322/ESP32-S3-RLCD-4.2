// 管理整点提醒、配网提示音和音频播放外设生命周期。
#include "audio_services.h"

#include "sensor_services.h"

#include <new>

#define AUDIO_TASK_FUNCTION_UNAVAILABLE_LOG_FORMAT "failed to create %s task: task function unavailable"
#define AUDIO_TASK_CREATE_FAILED_LOG_FORMAT "failed to create %s task rtos=%s stack=%u priority=%u core=%d rc=%d"
#define HOURLY_CHIME_PLAYED_LOG_FORMAT "hourly chime played sound=%d volume=%d"
#define HOURLY_CHIME_SKIPPED_LOG_FORMAT "hourly chime skipped sound=%d"

namespace {
constexpr uint32_t kAudioPlaybackTaskStack = 6144;
constexpr uint32_t kSettingsChimeRetryTaskStack = 3072;
constexpr UBaseType_t kAudioPlaybackTaskPriority = 4;
constexpr UBaseType_t kSettingsChimeRetryTaskPriority = 3;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr int kSettingsChimeRetryAttempts = 8;
constexpr uint32_t kSettingsChimeRetryDelayMs = 180;
constexpr uint32_t kSetupPromptChainDelayMs = 120;
constexpr TickType_t kSettingsChimeRetryDelay = pdMS_TO_TICKS(kSettingsChimeRetryDelayMs);
constexpr TickType_t kSetupPromptChainDelay = pdMS_TO_TICKS(kSetupPromptChainDelayMs);
constexpr int kHourlyChimeQuietStartHour = 7;
constexpr int kHourlyChimeQuietEndHour = 22;
constexpr const char *kAudioCodecBoardName = "S3_RLCD_4_2";
constexpr const char *kDefaultAudioTaskName = "audio_play";
constexpr const char *kHourlyChimeTaskName = "hourly_chime";
constexpr const char *kSetupPromptTaskName = "setup_prompt";
constexpr const char *kSettingsChimeRetryTaskName = "settings_chime";
constexpr const char *kDefaultAudioLogName = "audio playback";
constexpr const char *kHourlyChimeLogName = "hourly chime";
constexpr const char *kSetupPromptLogName = "setup prompt";
constexpr const char *kSettingsChimeBusyLog = "settings confirmation chime skipped: audio busy";
constexpr const char *kAudioCodecAllocationFailedLog = "audio codec allocation failed";
constexpr const char *kSetupPromptPlayedLog = "setup prompt played";
constexpr const char *kSetupPromptSkippedLog = "setup prompt skipped";
constexpr const char *kSetupPromptPendingLog = "setup prompt pending";
constexpr const char *kSettingsChimeRetryTaskCreateFailedLog = "failed to create settings chime retry task";
constexpr const char *kHourlyChimeRadioSetupSkippedLog = "hourly chime skipped while radio or setup is active";
constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

static_assert(kAudioPlaybackTaskStack > 0, "audio playback task stack must be positive");
static_assert(kSettingsChimeRetryTaskStack > 0, "settings chime retry task stack must be positive");
static_assert(kAudioPlaybackTaskPriority > tskIDLE_PRIORITY, "audio playback task priority must exceed idle");
static_assert(kSettingsChimeRetryTaskPriority > tskIDLE_PRIORITY, "settings chime retry priority must exceed idle");
static_assert(kAudioTaskCore >= 0, "audio task core must be non-negative");
static_assert(kSettingsChimeRetryAttempts > 0, "settings chime retry attempts must be positive");
static_assert(kSettingsChimeRetryDelayMs > 0, "settings chime retry delay must be positive");
static_assert(kSetupPromptChainDelayMs > 0, "setup prompt chain delay must be positive");
static_assert(kSettingsChimeRetryDelay > 0, "settings chime retry delay must be positive");
static_assert(kSetupPromptChainDelay > 0, "setup prompt chain delay must be positive");
static_assert(kHourlyChimeQuietStartHour >= 0 && kHourlyChimeQuietStartHour < 24,
              "hourly chime start hour must be in 0..23");
static_assert(kHourlyChimeQuietEndHour >= 0 && kHourlyChimeQuietEndHour < 24,
              "hourly chime end hour must be in 0..23");
static_assert(kHourlyChimeQuietStartHour <= kHourlyChimeQuietEndHour,
              "hourly chime active window must not wrap midnight");
static_assert(cstr_nonempty(kAudioCodecBoardName), "audio codec board name must be non-empty");
static_assert(cstr_nonempty(kDefaultAudioTaskName), "default audio task name must be non-empty");
static_assert(cstr_nonempty(kHourlyChimeTaskName), "hourly chime task name must be non-empty");
static_assert(cstr_nonempty(kSetupPromptTaskName), "setup prompt task name must be non-empty");
static_assert(cstr_nonempty(kSettingsChimeRetryTaskName), "settings chime task name must be non-empty");
static_assert(cstr_nonempty(kDefaultAudioLogName), "default audio log name must be non-empty");
static_assert(cstr_nonempty(kHourlyChimeLogName), "hourly chime log name must be non-empty");
static_assert(cstr_nonempty(kSetupPromptLogName), "setup prompt log name must be non-empty");
static_assert(cstr_nonempty(kSettingsChimeBusyLog), "settings chime busy log must be non-empty");
static_assert(cstr_nonempty(kAudioCodecAllocationFailedLog), "audio codec allocation log must be non-empty");
static_assert(cstr_nonempty(kSetupPromptPlayedLog), "setup prompt played log must be non-empty");
static_assert(cstr_nonempty(kSetupPromptSkippedLog), "setup prompt skipped log must be non-empty");
static_assert(cstr_nonempty(kSetupPromptPendingLog), "setup prompt pending log must be non-empty");
static_assert(cstr_nonempty(kSettingsChimeRetryTaskCreateFailedLog),
              "settings chime retry task failure log must be non-empty");
static_assert(cstr_nonempty(kHourlyChimeRadioSetupSkippedLog),
              "hourly chime radio/setup skip log must be non-empty");
} // namespace

bool try_mark_audio_playing()
{
    bool acquired = false;
    portENTER_CRITICAL(&g_audio_state_mux);
    if (!g_audio_playing) {
        g_audio_playing = true;
        acquired = true;
    }
    portEXIT_CRITICAL(&g_audio_state_mux);
    return acquired;
}

void clear_audio_playing()
{
    portENTER_CRITICAL(&g_audio_state_mux);
    g_audio_playing = false;
    portEXIT_CRITICAL(&g_audio_state_mux);
}

bool is_audio_playing()
{
    bool playing = false;
    portENTER_CRITICAL(&g_audio_state_mux);
    playing = g_audio_playing;
    portEXIT_CRITICAL(&g_audio_state_mux);
    return playing;
}

static CodecPort *ensure_audio_codec()
{
    if (!g_codec) {
        g_codec = new (std::nothrow) CodecPort(g_i2c, kAudioCodecBoardName);
        if (!g_codec) {
            ESP_LOGW(TAG, "%s", kAudioCodecAllocationFailedLog);
        }
    }
    return g_codec;
}

static void release_audio_codec()
{
    if (g_codec) {
        delete g_codec;
        g_codec = nullptr;
    }
}

static void finish_audio_playback()
{
    release_audio_codec();
    release_audio_awake_lock();
    clear_audio_playing();
}

static bool audio_blocked_by_system_state()
{
    return g_low_battery_mode || g_ota_state == kOtaUpdating;
}

static bool outside_hourly_chime_window(int hour)
{
    return hour < kHourlyChimeQuietStartHour || hour > kHourlyChimeQuietEndHour;
}

static bool create_audio_playback_task(TaskFunction_t task_fn,
                                       const char *task_name,
                                       void *task_arg,
                                       const char *log_name)
{
    const char *display_name = log_name ? log_name : kDefaultAudioLogName;
    const char *rtos_name = task_name ? task_name : kDefaultAudioTaskName;
    if (!task_fn) {
        ESP_LOGW(TAG, AUDIO_TASK_FUNCTION_UNAVAILABLE_LOG_FORMAT, display_name);
        return false;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(task_fn,
                                            rtos_name,
                                            kAudioPlaybackTaskStack,
                                            task_arg,
                                            kAudioPlaybackTaskPriority,
                                            nullptr,
                                            kAudioTaskCore);
    if (ok != pdPASS) {
        ESP_LOGW(TAG,
                 AUDIO_TASK_CREATE_FAILED_LOG_FORMAT,
                 display_name,
                 rtos_name,
                 (unsigned)kAudioPlaybackTaskStack,
                 (unsigned)kAudioPlaybackTaskPriority,
                 (int)kAudioTaskCore,
                 (int)ok);
        return false;
    }
    return true;
}

void hourly_chime_task(void *arg)
{
    int sound_index = (int)(intptr_t)arg;
    acquire_audio_awake_lock();
    CodecPort *codec = ensure_audio_codec();
    if (codec && codec->CodecPort_PlayChimeSound(sound_index, g_chime_volume_percent)) {
        ESP_LOGI(TAG, HOURLY_CHIME_PLAYED_LOG_FORMAT, sound_index, g_chime_volume_percent);
    } else {
        ESP_LOGW(TAG, HOURLY_CHIME_SKIPPED_LOG_FORMAT, sound_index);
    }
    finish_audio_playback();
    if (g_setup_prompt_pending && !g_startup_screen_active) {
        vTaskDelay(kSetupPromptChainDelay);
        (void)start_setup_prompt_playback();
    }
    vTaskDelete(nullptr);
}

void setup_prompt_task(void *)
{
    acquire_audio_awake_lock();
    CodecPort *codec = ensure_audio_codec();
    if (codec && codec->CodecPort_PlayWifiPrompt()) {
        ESP_LOGI(TAG, "%s", kSetupPromptPlayedLog);
    } else {
        ESP_LOGW(TAG, "%s", kSetupPromptSkippedLog);
    }
    finish_audio_playback();
    vTaskDelete(nullptr);
}

void settings_confirmation_chime_task(void *)
{
    for (int attempt = 0; attempt < kSettingsChimeRetryAttempts; ++attempt) {
        if (start_chime_playback(g_chime_sound_index)) {
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(kSettingsChimeRetryDelay);
    }
    ESP_LOGW(TAG, "%s", kSettingsChimeBusyLog);
    vTaskDelete(nullptr);
}

bool start_chime_playback(int source_slot)
{
    if (!try_mark_audio_playing()) {
        return false;
    }
    if (!create_audio_playback_task(hourly_chime_task,
                                    kHourlyChimeTaskName,
                                    (void *)(intptr_t)source_slot,
                                    kHourlyChimeLogName)) {
        clear_audio_playing();
        return false;
    }
    return true;
}

bool start_setup_prompt_playback()
{
    if (!try_mark_audio_playing()) {
        return false;
    }
    g_setup_prompt_pending = false;
    if (!create_audio_playback_task(setup_prompt_task,
                                    kSetupPromptTaskName,
                                    nullptr,
                                    kSetupPromptLogName)) {
        clear_audio_playing();
        g_setup_prompt_pending = true;
        return false;
    }
    return true;
}

void request_setup_prompt_once()
{
    if (g_startup_screen_active || is_audio_playing()) {
        g_setup_prompt_pending = true;
        ESP_LOGI(TAG, "%s", kSetupPromptPendingLog);
        return;
    }
    if (!start_setup_prompt_playback()) {
        g_setup_prompt_pending = true;
    }
}

void request_settings_confirmation_chime()
{
    if (audio_blocked_by_system_state()) {
        return;
    }
    if (start_chime_playback(g_chime_sound_index)) {
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(settings_confirmation_chime_task,
                                            kSettingsChimeRetryTaskName,
                                            kSettingsChimeRetryTaskStack,
                                            nullptr,
                                            kSettingsChimeRetryTaskPriority,
                                            nullptr,
                                            kAudioTaskCore);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "%s", kSettingsChimeRetryTaskCreateFailedLog);
    }
}

void play_hourly_chime(int hour, bool enforce_quiet_hours)
{
    if (audio_blocked_by_system_state()) {
        return;
    }
    if (g_wifi_radio_on || g_setup_portal_active || g_ota_state == kOtaChecking) {
        ESP_LOGI(TAG, "%s", kHourlyChimeRadioSetupSkippedLog);
        return;
    }
    if (enforce_quiet_hours && !g_hourly_chime_all_day && outside_hourly_chime_window(hour)) {
        return;
    }
    (void)start_chime_playback(g_chime_sound_index);
}
