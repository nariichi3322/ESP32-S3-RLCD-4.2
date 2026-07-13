// 播放小智首次绑定提示和逐位数字语音，并管理独立播放任务。
#include "xiaozhi_binding_voice.h"

#include "app_state.h"
#include "audio_services.h"

#include <esp_log.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string.h>

namespace {
constexpr int kBindingPcmSampleRate = 16000;
constexpr size_t kBindingPauseSamples = 1280;
constexpr uint32_t kBindingVoiceTaskStackBytes = 6144;
constexpr UBaseType_t kBindingVoiceTaskPriority = 3;
constexpr size_t kBindingCodeStorageSize = 24;
#define XIAOZHI_BINDING_COPY_ALLOC_FAILED_LOG "xiaozhi binding code copy allocation failed"
#define XIAOZHI_BINDING_TASK_CREATE_FAILED_LOG "xiaozhi binding voice task creation failed"

static_assert(kBindingVoiceTaskStackBytes > 0,
              "Xiaozhi binding task stack must be positive");
static_assert(kBindingCodeStorageSize > 1,
              "Xiaozhi binding code storage must fit text and NUL");

extern const uint8_t prompt_pcm_start[] asm("_binary_prompt_pcm_start");
extern const uint8_t prompt_pcm_end[] asm("_binary_prompt_pcm_end");
extern const uint8_t digit_0_pcm_start[] asm("_binary_digit_0_pcm_start");
extern const uint8_t digit_0_pcm_end[] asm("_binary_digit_0_pcm_end");
extern const uint8_t digit_1_pcm_start[] asm("_binary_digit_1_pcm_start");
extern const uint8_t digit_1_pcm_end[] asm("_binary_digit_1_pcm_end");
extern const uint8_t digit_2_pcm_start[] asm("_binary_digit_2_pcm_start");
extern const uint8_t digit_2_pcm_end[] asm("_binary_digit_2_pcm_end");
extern const uint8_t digit_3_pcm_start[] asm("_binary_digit_3_pcm_start");
extern const uint8_t digit_3_pcm_end[] asm("_binary_digit_3_pcm_end");
extern const uint8_t digit_4_pcm_start[] asm("_binary_digit_4_pcm_start");
extern const uint8_t digit_4_pcm_end[] asm("_binary_digit_4_pcm_end");
extern const uint8_t digit_5_pcm_start[] asm("_binary_digit_5_pcm_start");
extern const uint8_t digit_5_pcm_end[] asm("_binary_digit_5_pcm_end");
extern const uint8_t digit_6_pcm_start[] asm("_binary_digit_6_pcm_start");
extern const uint8_t digit_6_pcm_end[] asm("_binary_digit_6_pcm_end");
extern const uint8_t digit_7_pcm_start[] asm("_binary_digit_7_pcm_start");
extern const uint8_t digit_7_pcm_end[] asm("_binary_digit_7_pcm_end");
extern const uint8_t digit_8_pcm_start[] asm("_binary_digit_8_pcm_start");
extern const uint8_t digit_8_pcm_end[] asm("_binary_digit_8_pcm_end");
extern const uint8_t digit_9_pcm_start[] asm("_binary_digit_9_pcm_start");
extern const uint8_t digit_9_pcm_end[] asm("_binary_digit_9_pcm_end");

struct EmbeddedPcm {
    const uint8_t *start;
    const uint8_t *end;
};

constexpr EmbeddedPcm kBindingPromptPcm = {prompt_pcm_start, prompt_pcm_end};
constexpr EmbeddedPcm kBindingDigitPcm[] = {
    {digit_0_pcm_start, digit_0_pcm_end},
    {digit_1_pcm_start, digit_1_pcm_end},
    {digit_2_pcm_start, digit_2_pcm_end},
    {digit_3_pcm_start, digit_3_pcm_end},
    {digit_4_pcm_start, digit_4_pcm_end},
    {digit_5_pcm_start, digit_5_pcm_end},
    {digit_6_pcm_start, digit_6_pcm_end},
    {digit_7_pcm_start, digit_7_pcm_end},
    {digit_8_pcm_start, digit_8_pcm_end},
    {digit_9_pcm_start, digit_9_pcm_end},
};
static_assert(sizeof(kBindingDigitPcm) / sizeof(kBindingDigitPcm[0]) == 10,
              "Xiaozhi binding digit audio must cover 0 through 9");

char s_last_announced_binding_code[kBindingCodeStorageSize] = {};

bool play_embedded_pcm(const EmbeddedPcm &pcm)
{
    if (!pcm.start || !pcm.end || pcm.end <= pcm.start) {
        return false;
    }
    size_t bytes = static_cast<size_t>(pcm.end - pcm.start);
    if (bytes % sizeof(int16_t) != 0) {
        return false;
    }
    return write_xiaozhi_speaker(reinterpret_cast<const int16_t *>(pcm.start),
                                 bytes / sizeof(int16_t),
                                 kBindingPcmSampleRate) == ESP_CODEC_DEV_OK;
}

void binding_id_voice_task(void *arg)
{
    char *binding_code = static_cast<char *>(arg);
    if (!binding_code) {
        vTaskDelete(nullptr);
        return;
    }
    if (!start_xiaozhi_audio_session()) {
        free(binding_code);
        vTaskDelete(nullptr);
        return;
    }
    bool played = play_embedded_pcm(kBindingPromptPcm);
    static const int16_t silence[kBindingPauseSamples] = {};
    if (played) {
        played = write_xiaozhi_speaker(silence,
                                       kBindingPauseSamples,
                                       kBindingPcmSampleRate) == ESP_CODEC_DEV_OK;
    }
    for (const char *cursor = binding_code; played && *cursor; ++cursor) {
        int index = xiaozhi_binding_voice::digit_index(*cursor);
        if (index < 0) {
            continue;
        }
        played = play_embedded_pcm(kBindingDigitPcm[index]);
        if (played) {
            played = write_xiaozhi_speaker(silence,
                                           kBindingPauseSamples,
                                           kBindingPcmSampleRate) == ESP_CODEC_DEV_OK;
        }
    }
    ESP_LOGI(TAG, "xiaozhi binding code playback %s", played ? "complete" : "failed");
    stop_xiaozhi_audio_session();
    free(binding_code);
    vTaskDelete(nullptr);
}
} // namespace

void xiaozhi_announce_binding_id_once(const char *binding_code)
{
    if (!xiaozhi_binding_voice::should_announce(binding_code,
                                                s_last_announced_binding_code)) {
        return;
    }
    char *code_copy = static_cast<char *>(calloc(1, sizeof(s_last_announced_binding_code)));
    if (!code_copy) {
        ESP_LOGW(TAG, XIAOZHI_BINDING_COPY_ALLOC_FAILED_LOG);
        return;
    }
    strlcpy(code_copy, binding_code, sizeof(s_last_announced_binding_code));
    if (xTaskCreate(binding_id_voice_task,
                    "xiaozhi_bind",
                    kBindingVoiceTaskStackBytes,
                    code_copy,
                    kBindingVoiceTaskPriority,
                    nullptr) != pdPASS) {
        ESP_LOGW(TAG, XIAOZHI_BINDING_TASK_CREATE_FAILED_LOG);
        free(code_copy);
        return;
    }
    strlcpy(s_last_announced_binding_code,
            binding_code,
            sizeof(s_last_announced_binding_code));
}
