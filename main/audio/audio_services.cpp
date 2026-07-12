// 管理整点提醒、配网提示音和音频播放外设生命周期。
#include "audio_services.h"

#include "app_constexpr.h"
#include "sensor_services.h"

#include <cstddef>
#include <new>

#include "driver/gpio.h"

#define AUDIO_TASK_FUNCTION_UNAVAILABLE_LOG_FORMAT "failed to create %s task: task function unavailable"
#define AUDIO_TASK_CREATE_FAILED_LOG_FORMAT "failed to create %s task rtos=%s stack=%u priority=%u core=%d rc=%d"
#define HOURLY_CHIME_PLAYED_LOG_FORMAT "hourly chime played sound=%d volume=%d"
#define HOURLY_CHIME_SKIPPED_LOG_FORMAT "hourly chime skipped sound=%d"
#define AUDIO_IDLE_GPIO_CONFIG_FAILED_LOG_FORMAT "audio idle gpio config failed pin=%d err=%s"
#define AUDIO_IDLE_GPIO_LEVEL_FAILED_LOG_FORMAT "audio idle gpio level failed pin=%d err=%s"

namespace {
constexpr uint32_t kAudioPlaybackTaskStack = 6144;
constexpr uint32_t kSettingsChimeRetryTaskStack = 3072;
constexpr UBaseType_t kAudioPlaybackTaskPriority = 4;
constexpr UBaseType_t kSettingsChimeRetryTaskPriority = 3;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr int kSettingsChimeRetryAttempts = 8;
constexpr float kXiaozhiMicGainDb = 37.5f;
constexpr int kXiaozhiAudioSampleRate = 16000;
constexpr size_t kXiaozhiSpeakerFadeSamples = 160;
constexpr size_t kXiaozhiSpeakerTailSilenceSamples = 160;
constexpr uint32_t kSettingsChimeRetryDelayMs = 180;
constexpr uint32_t kSetupPromptChainDelayMs = 120;
constexpr TickType_t kSettingsChimeRetryDelay = pdMS_TO_TICKS(kSettingsChimeRetryDelayMs);
constexpr TickType_t kSetupPromptChainDelay = pdMS_TO_TICKS(kSetupPromptChainDelayMs);
constexpr int kHourlyChimeQuietStartHour = 7;
constexpr int kHourlyChimeQuietEndHour = 22;
constexpr gpio_num_t kAudioMclkGpio = GPIO_NUM_16;
constexpr gpio_num_t kAudioBclkGpio = GPIO_NUM_9;
constexpr gpio_num_t kAudioWsGpio = GPIO_NUM_45;
constexpr gpio_num_t kAudioDinGpio = GPIO_NUM_10;
constexpr gpio_num_t kAudioDoutGpio = GPIO_NUM_8;
constexpr gpio_num_t kAudioPaGpio = GPIO_NUM_46;
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
constexpr const char *kXiaozhiAudioStartFailedLog = "xiaozhi audio session start failed";
constexpr const char *kSetupPromptPlayedLog = "setup prompt played";
constexpr const char *kSetupPromptSkippedLog = "setup prompt skipped";
constexpr const char *kSetupPromptPendingLog = "setup prompt pending";
constexpr const char *kSettingsChimeRetryTaskCreateFailedLog = "failed to create settings chime retry task";
constexpr const char *kHourlyChimeRadioSetupSkippedLog = "hourly chime skipped while radio or setup is active";
constexpr const char *kAudioTexts[] = {
    kAudioCodecBoardName,
    kDefaultAudioTaskName,
    kHourlyChimeTaskName,
    kSetupPromptTaskName,
    kSettingsChimeRetryTaskName,
    kDefaultAudioLogName,
    kHourlyChimeLogName,
    kSetupPromptLogName,
    kSettingsChimeBusyLog,
    kAudioCodecAllocationFailedLog,
    kXiaozhiAudioStartFailedLog,
    kSetupPromptPlayedLog,
    kSetupPromptSkippedLog,
    kSetupPromptPendingLog,
    kSettingsChimeRetryTaskCreateFailedLog,
    kHourlyChimeRadioSetupSkippedLog,
    AUDIO_TASK_FUNCTION_UNAVAILABLE_LOG_FORMAT,
    AUDIO_TASK_CREATE_FAILED_LOG_FORMAT,
    HOURLY_CHIME_PLAYED_LOG_FORMAT,
    HOURLY_CHIME_SKIPPED_LOG_FORMAT,
    AUDIO_IDLE_GPIO_CONFIG_FAILED_LOG_FORMAT,
    AUDIO_IDLE_GPIO_LEVEL_FAILED_LOG_FORMAT,
};

static_assert(kAudioPlaybackTaskStack > 0, "audio playback task stack must be positive");
static_assert(kSettingsChimeRetryTaskStack > 0, "settings chime retry task stack must be positive");
static_assert(kAudioPlaybackTaskPriority > tskIDLE_PRIORITY, "audio playback task priority must exceed idle");
static_assert(kSettingsChimeRetryTaskPriority > tskIDLE_PRIORITY, "settings chime retry priority must exceed idle");
static_assert(kAudioTaskCore >= 0, "audio task core must be non-negative");
static_assert(kAudioTaskCore < portNUM_PROCESSORS, "audio task core must exist on the target");
static_assert(kSettingsChimeRetryAttempts > 0, "settings chime retry attempts must be positive");
static_assert(kSettingsChimeRetryDelayMs > 0, "settings chime retry delay must be positive");
static_assert(kSetupPromptChainDelayMs > 0, "setup prompt chain delay must be positive");
static_assert(kSettingsChimeRetryDelay > 0, "settings chime retry delay must be positive");
static_assert(kSetupPromptChainDelay > 0, "setup prompt chain delay must be positive");
static_assert(kXiaozhiAudioSampleRate > 0, "xiaozhi sample rate must be positive");
static_assert(kXiaozhiSpeakerFadeSamples > 0, "xiaozhi speaker fade must be positive");
static_assert(kXiaozhiSpeakerTailSilenceSamples > 0, "xiaozhi speaker tail must be positive");
static_assert(kHourlyChimeQuietStartHour >= 0 && kHourlyChimeQuietStartHour < 24,
              "hourly chime start hour must be in 0..23");
static_assert(kHourlyChimeQuietEndHour >= 0 && kHourlyChimeQuietEndHour < 24,
              "hourly chime end hour must be in 0..23");
static_assert(kHourlyChimeQuietStartHour <= kHourlyChimeQuietEndHour,
              "hourly chime active window must not wrap midnight");
static_assert(kAudioMclkGpio >= GPIO_NUM_0, "audio MCLK GPIO must be valid");
static_assert(kAudioBclkGpio >= GPIO_NUM_0, "audio BCLK GPIO must be valid");
static_assert(kAudioWsGpio >= GPIO_NUM_0, "audio WS GPIO must be valid");
static_assert(kAudioDinGpio >= GPIO_NUM_0, "audio DIN GPIO must be valid");
static_assert(kAudioDoutGpio >= GPIO_NUM_0, "audio DOUT GPIO must be valid");
static_assert(kAudioPaGpio >= GPIO_NUM_0, "audio PA GPIO must be valid");
static_assert(array_count(kAudioTexts) > 0, "audio text registry must not be empty");
static_assert(cstr_array_nonempty(kAudioTexts), "audio task names, log names and log formats must be non-empty");
} // namespace

extern const uint8_t xiaozhi_popup_pcm_start[] asm("_binary_popup_pcm_start");
extern const uint8_t xiaozhi_popup_pcm_end[] asm("_binary_popup_pcm_end");

static bool s_xiaozhi_speaker_stream_active = false;
static bool s_xiaozhi_speaker_open = false;
static size_t s_xiaozhi_speaker_fade_progress = 0;
static int16_t s_xiaozhi_last_speaker_sample = 0;
static int s_xiaozhi_applied_volume = -1;
static void finish_xiaozhi_speaker_stream();

static void configure_audio_idle_gpio(gpio_num_t pin, gpio_mode_t mode, gpio_pulldown_t pull_down)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = mode;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = pull_down;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, AUDIO_IDLE_GPIO_CONFIG_FAILED_LOG_FORMAT, (int)pin, esp_err_to_name(err));
        return;
    }
    if (mode == GPIO_MODE_OUTPUT) {
        err = gpio_set_level(pin, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, AUDIO_IDLE_GPIO_LEVEL_FAILED_LOG_FORMAT, (int)pin, esp_err_to_name(err));
        }
    }
}

static void configure_audio_idle_output(gpio_num_t pin)
{
    configure_audio_idle_gpio(pin, GPIO_MODE_OUTPUT, GPIO_PULLDOWN_DISABLE);
}

static void configure_audio_idle_input(gpio_num_t pin)
{
    configure_audio_idle_gpio(pin, GPIO_MODE_INPUT, GPIO_PULLDOWN_ENABLE);
}

void park_unused_audio_peripherals()
{
    configure_audio_idle_output(kAudioPaGpio);
    configure_audio_idle_output(kAudioMclkGpio);
    configure_audio_idle_output(kAudioBclkGpio);
    configure_audio_idle_output(kAudioWsGpio);
    configure_audio_idle_output(kAudioDoutGpio);
    configure_audio_idle_input(kAudioDinGpio);
}

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
    park_unused_audio_peripherals();
    release_audio_awake_lock();
    clear_audio_playing();
}

bool start_xiaozhi_audio_session()
{
    if (!try_mark_audio_playing()) {
        return false;
    }
    acquire_audio_awake_lock();
    CodecPort *codec = ensure_audio_codec();
    if (!codec || !codec->CodecPort_OpenXiaozhiMic()) {
        ESP_LOGW(TAG, "%s", kXiaozhiAudioStartFailedLog);
        finish_audio_playback();
        return false;
    }
    codec->CodecPort_SetMicGain(kXiaozhiMicGainDb);
    s_xiaozhi_speaker_stream_active = false;
    s_xiaozhi_speaker_open = false;
    s_xiaozhi_speaker_fade_progress = 0;
    s_xiaozhi_last_speaker_sample = 0;
    s_xiaozhi_applied_volume = -1;
    return true;
}

void stop_xiaozhi_audio_session()
{
    if (!is_audio_playing()) {
        return;
    }
    if (g_codec) {
        finish_xiaozhi_speaker_stream();
        g_codec->CodecPort_CloseSpeaker();
        g_codec->CodecPort_CloseMic();
    }
    s_xiaozhi_speaker_open = false;
    s_xiaozhi_applied_volume = -1;
    finish_audio_playback();
}

void set_xiaozhi_audio_high_performance(bool enabled)
{
    if (is_audio_playing()) {
        set_audio_performance_mode(enabled);
    }
}

int read_xiaozhi_microphone(void *buffer, size_t bytes)
{
    if (!g_codec || !buffer || bytes == 0) {
        return ESP_FAIL;
    }
    return g_codec->CodecPort_EchoRead(buffer, static_cast<int>(bytes));
}

int write_xiaozhi_speaker(const int16_t *mono_samples, size_t sample_count, int sample_rate)
{
    if (!g_codec || !mono_samples || sample_count == 0 || !g_codec->CodecPort_OpenXiaozhiSpeaker(sample_rate)) {
        return ESP_FAIL;
    }
    s_xiaozhi_speaker_open = true;
    apply_xiaozhi_speaker_volume(g_chime_volume_percent);
    // 官方同板卡使用标准单声道 TX；RX 的四时隙 TDM 麦克风/参考声道
    // 与播放并行运行，因此这里直接写入服务器提供的 mono PCM。
    constexpr size_t kFramesPerChunk = 160;
    int16_t mono[kFramesPerChunk] = {};
    size_t offset = 0;
    while (offset < sample_count) {
        size_t frames = sample_count - offset;
        if (frames > kFramesPerChunk) {
            frames = kFramesPerChunk;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            int16_t sample = mono_samples[offset + frame];
            if (s_xiaozhi_speaker_fade_progress < kXiaozhiSpeakerFadeSamples) {
                sample = static_cast<int16_t>(
                    (static_cast<int32_t>(sample) *
                     static_cast<int32_t>(s_xiaozhi_speaker_fade_progress)) /
                    static_cast<int32_t>(kXiaozhiSpeakerFadeSamples));
                ++s_xiaozhi_speaker_fade_progress;
            }
            mono[frame] = sample;
            s_xiaozhi_last_speaker_sample = sample;
            s_xiaozhi_speaker_stream_active = true;
        }
        int written = g_codec->CodecPort_PlayWrite(mono, static_cast<int>(frames * sizeof(int16_t)));
        if (written != ESP_CODEC_DEV_OK) {
            return written;
        }
        offset += frames;
    }
    return ESP_CODEC_DEV_OK;
}

void apply_xiaozhi_speaker_volume(int volume_percent)
{
    if (volume_percent < 0) {
        volume_percent = 0;
    } else if (volume_percent > 100) {
        volume_percent = 100;
    }
    if (!g_codec || !s_xiaozhi_speaker_open || s_xiaozhi_applied_volume == volume_percent) {
        return;
    }
    g_codec->CodecPort_SetSpeakerVol(volume_percent);
    s_xiaozhi_applied_volume = volume_percent;
}

static void finish_xiaozhi_speaker_stream()
{
    if (!g_codec || !s_xiaozhi_speaker_stream_active) {
        s_xiaozhi_speaker_fade_progress = 0;
        s_xiaozhi_last_speaker_sample = 0;
        return;
    }
    int16_t tail[kXiaozhiSpeakerFadeSamples + kXiaozhiSpeakerTailSilenceSamples] = {};
    for (size_t index = 0; index < kXiaozhiSpeakerFadeSamples; ++index) {
        tail[index] = static_cast<int16_t>(
            (static_cast<int32_t>(s_xiaozhi_last_speaker_sample) *
             static_cast<int32_t>(kXiaozhiSpeakerFadeSamples - index - 1)) /
            static_cast<int32_t>(kXiaozhiSpeakerFadeSamples));
    }
    (void)write_xiaozhi_speaker(tail,
                                sizeof(tail) / sizeof(tail[0]),
                                kXiaozhiAudioSampleRate);
    s_xiaozhi_speaker_stream_active = false;
    s_xiaozhi_speaker_fade_progress = 0;
    s_xiaozhi_last_speaker_sample = 0;
}

bool resume_xiaozhi_microphone_after_playback()
{
    if (!g_codec) {
        return false;
    }
    // STD TX 与 TDM RX 是官方同板卡验证过的全双工布局。结束播放只关闭
    // 扬声器，麦克风/AEC 流保持连续，避免丢失用户插话的开头。
    finish_xiaozhi_speaker_stream();
    g_codec->CodecPort_CloseSpeaker();
    s_xiaozhi_speaker_open = false;
    s_xiaozhi_applied_volume = -1;
    ESP_LOGI(TAG, "xiaozhi duplex microphone kept active");
    return true;
}

bool play_xiaozhi_wake_feedback()
{
    // This is the upstream 78/xiaozhi-esp32 assets/common/popup.ogg converted
    // to 16 kHz mono PCM so the existing shared codec path can play it without
    // importing the upstream Ogg demuxer or a second audio framework.
    size_t pcm_bytes = static_cast<size_t>(xiaozhi_popup_pcm_end - xiaozhi_popup_pcm_start);
    bool pcm_valid = pcm_bytes > 0 && (pcm_bytes % sizeof(int16_t)) == 0;
    bool played = pcm_valid &&
                  write_xiaozhi_speaker(
                      reinterpret_cast<const int16_t *>(xiaozhi_popup_pcm_start),
                      pcm_bytes / sizeof(int16_t),
                      kXiaozhiAudioSampleRate) == ESP_CODEC_DEV_OK;
    bool restored = resume_xiaozhi_microphone_after_playback();
    ESP_LOGI(TAG, "xiaozhi wake feedback: played=%d microphone=%d", played, restored);
    return played && restored;
}

void smooth_xiaozhi_speaker_segment_transition()
{
    // sentence_start messages are serialized between the preceding and next
    // audio packets, so this can safely finish the old waveform without
    // closing the codec or toggling the PA.
    finish_xiaozhi_speaker_stream();
}

void abort_xiaozhi_speaker_playback()
{
    if (!g_codec) {
        return;
    }
    finish_xiaozhi_speaker_stream();
    g_codec->CodecPort_CloseSpeaker();
    s_xiaozhi_speaker_open = false;
    s_xiaozhi_applied_volume = -1;
    ESP_LOGI(TAG, "xiaozhi speaker playback aborted");
}

static CodecPort *prepare_audio_codec_for_playback()
{
    acquire_audio_awake_lock();
    return ensure_audio_codec();
}

static bool audio_blocked_by_system_state()
{
    return g_low_battery_mode || g_ota_state == kOtaUpdating;
}

static bool outside_hourly_chime_window(int hour)
{
    return hour < kHourlyChimeQuietStartHour || hour > kHourlyChimeQuietEndHour;
}

static bool hourly_chime_blocked_by_network_activity()
{
    return g_wifi_radio_on || g_setup_portal_active || g_ota_state == kOtaChecking;
}

static const char *audio_text_or_default(const char *text, const char *fallback)
{
    return text ? text : fallback;
}

void settings_confirmation_chime_task(void *);

static bool create_audio_task(TaskFunction_t task_fn,
                              const char *task_name,
                              uint32_t task_stack,
                              UBaseType_t task_priority,
                              void *task_arg,
                              const char *log_name)
{
    const char *display_name = audio_text_or_default(log_name, kDefaultAudioLogName);
    const char *rtos_name = audio_text_or_default(task_name, kDefaultAudioTaskName);
    if (!task_fn) {
        ESP_LOGW(TAG, AUDIO_TASK_FUNCTION_UNAVAILABLE_LOG_FORMAT, display_name);
        return false;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(task_fn,
                                            rtos_name,
                                            task_stack,
                                            task_arg,
                                            task_priority,
                                            nullptr,
                                            kAudioTaskCore);
    if (ok != pdPASS) {
        ESP_LOGW(TAG,
                 AUDIO_TASK_CREATE_FAILED_LOG_FORMAT,
                 display_name,
                 rtos_name,
                 (unsigned)task_stack,
                 (unsigned)task_priority,
                 (int)kAudioTaskCore,
                 (int)ok);
        return false;
    }
    return true;
}

static void create_settings_chime_retry_task()
{
    (void)create_audio_task(settings_confirmation_chime_task,
                            kSettingsChimeRetryTaskName,
                            kSettingsChimeRetryTaskStack,
                            kSettingsChimeRetryTaskPriority,
                            nullptr,
                            kSettingsChimeRetryTaskCreateFailedLog);
}

void hourly_chime_task(void *arg)
{
    int sound_index = (int)(intptr_t)arg;
    CodecPort *codec = prepare_audio_codec_for_playback();
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

bool play_chime_sound_blocking(int source_slot, bool (*stop_requested)())
{
    return play_chime_sound_repeated_blocking(source_slot, 1, stop_requested);
}

bool play_chime_sound_repeated_blocking(int source_slot,
                                        int repeat_count,
                                        bool (*stop_requested)())
{
    if (repeat_count <= 0 || !try_mark_audio_playing()) {
        return false;
    }
    CodecPort *codec = prepare_audio_codec_for_playback();
    bool played = codec != nullptr;
    for (int repeat = 0; played && repeat < repeat_count; ++repeat) {
        if (stop_requested && stop_requested()) {
            played = false;
            break;
        }
        played = codec->CodecPort_PlayChimeSound(source_slot,
                                                 g_chime_volume_percent,
                                                 stop_requested);
    }
    finish_audio_playback();
    return played;
}

void setup_prompt_task(void *)
{
    CodecPort *codec = prepare_audio_codec_for_playback();
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
    if (!create_audio_task(hourly_chime_task,
                           kHourlyChimeTaskName,
                           kAudioPlaybackTaskStack,
                           kAudioPlaybackTaskPriority,
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
    if (!create_audio_task(setup_prompt_task,
                           kSetupPromptTaskName,
                           kAudioPlaybackTaskStack,
                           kAudioPlaybackTaskPriority,
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
    create_settings_chime_retry_task();
}

void play_hourly_chime(int hour, bool enforce_quiet_hours)
{
    if (audio_blocked_by_system_state()) {
        return;
    }
    if (hourly_chime_blocked_by_network_activity()) {
        ESP_LOGI(TAG, "%s", kHourlyChimeRadioSetupSkippedLog);
        return;
    }
    if (enforce_quiet_hours && !g_hourly_chime_all_day && outside_hourly_chime_window(hour)) {
        return;
    }
    (void)start_chime_playback(g_chime_sound_index);
}
