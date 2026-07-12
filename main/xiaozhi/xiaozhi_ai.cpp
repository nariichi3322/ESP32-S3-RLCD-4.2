// 复用本项目网络与电源服务对接小智官方激活和 WebSocket 会话。
#include "xiaozhi_ai.h"

#include "app_state.h"
#include "alarm_services.h"
#include "audio_services.h"
#include "network_services.h"
#include "sensor_services.h"
#include "ui_views.h"
#include "xiaozhi_mcp.h"
#include "xiaozhi_protocol_utils.h"
#include "xiaozhi_voice.h"
#include "weather_city_mcp.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_crt_bundle.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ws.h>
#include <esp_ae_rate_cvt.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>
#include <freertos/stream_buffer.h>
#include <nvs.h>

#include <arpa/inet.h>
#include <atomic>
#include <string.h>

namespace {
constexpr size_t kActivationResponseSize = 3072;
constexpr size_t kActivationRequestSize = 1536;
constexpr size_t kDeviceIdSize = 18;
constexpr size_t kClientIdSize = 37;
constexpr uint32_t kWifiWaitMs = 30000;
constexpr uint32_t kActivationRetryMs = 15000;
constexpr uint32_t kActivationHttpTimeoutMs = 12000;
constexpr uint32_t kLoopIdleMs = 500;
constexpr uint32_t kWebsocketTimeoutMs = 12000;
constexpr uint32_t kConversationIdleTimeoutMs = 30000;
constexpr uint32_t kExitReplyTimeoutMs = 15000;
constexpr int kIncomingAudioBufferSize = 4096;
constexpr int kXiaozhiHardwareSampleRate = 16000;
constexpr size_t kOpusFrameSamples = 960;
// RFC 6716 limits one Opus packet to 1275 bytes. The three buffers together
// exceed 4 KiB, so keep the combined working set in PSRAM. ESP Audio Codec
// accesses them through ordinary CPU loads/stores and does not require DMA RAM.
constexpr size_t kOpusPacketCapacity = 1280;
constexpr size_t kWebsocketAudioHeaderCapacity = 16;
constexpr uint32_t kUserSubtitleMinVisibleMs = 2200;
constexpr size_t kTtsPlaybackStreamBytes = 32 * 1024;
constexpr size_t kTtsPlaybackChunkSamples = 960;
constexpr size_t kTtsPlaybackPrebufferBytes = kTtsPlaybackChunkSamples * sizeof(int16_t) * 3;
constexpr uint32_t kTtsPlaybackPrebufferWaitMs = 150;
constexpr uint32_t kTtsFinalFrameGraceMs = 350;
constexpr uint32_t kTtsPlaybackTailSettleMs = 120;
constexpr uint32_t kTtsPlaybackTaskStackBytes = 6144;
constexpr UBaseType_t kTtsPlaybackTaskPriority = 5;
// 官方实现为 Opus 编解码任务预留 24 KiB。这里的任务还负责 WebSocket
// 协议，因此至少保持相同栈空间，避免进入 SILK 编码器后破坏任务栈。
constexpr uint32_t kXiaozhiTaskStackSize = 24 * 1024;
constexpr int kBindingPcmSampleRate = 16000;
constexpr size_t kBindingPauseSamples = 1280;
constexpr uint32_t kBindingVoiceTaskStackBytes = 6144;
constexpr UBaseType_t kBindingVoiceTaskPriority = 3;
constexpr int kTtsPlaybackStopRetryCount = 200;
constexpr uint32_t kTtsPlaybackStopRetryDelayMs = 10;
constexpr const char *kNvsNamespace = "xiaozhi";
constexpr const char *kWebsocketUrlKey = "ws_url";
constexpr const char *kWebsocketTokenKey = "ws_token";
constexpr const char *kWebsocketVersionKey = "ws_ver";
constexpr const char *kActivationChallengeKey = "act_chal";
constexpr const char *kClientIdKey = "client_id";
constexpr const char *kBindingConfirmedKey = "bound_v1";
constexpr const char *kActivationUrl = CONFIG_XIAOZHI_AI_OTA_URL;
constexpr const char *kOfficialUserAgent = "ESP32-S3-RLCD-4.2/xiaozhi";
constexpr const char *kDefaultStatus = "小智准备中";
constexpr const char *kWifiStatus = "正在连接Wi-Fi";
constexpr const char *kActivatingStatus = "正在连接小智服务";
constexpr const char *kBindingStatus = "请绑定设备";
constexpr const char *kReadyStatus = "等待唤醒词";
constexpr const char *kErrorStatus = "小智服务不可用";
constexpr const char *kSpeakingStatus = "小智正在说话";
constexpr const char *kNeutralEmotion = "neutral";
constexpr const char *kNoWifiDetail = "请先在系统设置中配置 Wi-Fi";
constexpr const char *kOfflineDetail = "离线模式下无法使用小智 AI";
constexpr const char *kBoundDetail = "说出唤醒词即可开始对话";
constexpr const char *kWakeWordFailureDetail = "语音监听初始化失败，请稍后重试";
constexpr const char *kActivationFailureDetail = "稍后将自动重试";
constexpr const char *kBindingFallbackDetail = "请在小智服务中输入绑定 ID";
constexpr EventBits_t kAiPageActiveBit = BIT0;
constexpr EventBits_t kAiWakeBit = BIT1;
#define XIAOZHI_BINDING_COPY_ALLOC_FAILED_LOG "xiaozhi binding code copy allocation failed"
#define XIAOZHI_BINDING_TASK_CREATE_FAILED_LOG "xiaozhi binding voice task creation failed"
#define XIAOZHI_TTS_STREAM_CREATE_FAILED_LOG "xiaozhi TTS stream buffer creation failed"
#define XIAOZHI_TTS_TASK_CREATE_FAILED_LOG "xiaozhi TTS playback task creation failed"
#define XIAOZHI_STATE_INIT_FAILED_LOG "Xiaozhi AI state initialization failed"
#define XIAOZHI_NVS_CLEAR_OPEN_FAILED_FORMAT "xiaozhi activation NVS open for clear failed: %s"
#define XIAOZHI_NVS_CLEAR_ERASE_FAILED_FORMAT "xiaozhi activation NVS erase failed: %s"
#define XIAOZHI_NVS_CLEAR_COMMIT_FAILED_FORMAT "xiaozhi activation NVS clear commit failed: %s"
#define XIAOZHI_ACTIVATION_NVS_OPEN_FAILED_FORMAT "xiaozhi activation NVS open failed: %s"
#define XIAOZHI_ACTIVATION_NVS_SAVE_FAILED_FORMAT "xiaozhi activation NVS save failed: %s"
#define XIAOZHI_CLIENT_ID_NVS_OPEN_FAILED_FORMAT "xiaozhi client id NVS open failed: %s"
#define XIAOZHI_CLIENT_ID_NVS_SAVE_FAILED_FORMAT "xiaozhi client id NVS save failed: %s"
#define XIAOZHI_ACTIVATION_HEADER_FAILED_FORMAT "xiaozhi activation header %s failed: %s"
#define XIAOZHI_ACTIVATION_BODY_FAILED_FORMAT "xiaozhi activation body failed: %s"
#define XIAOZHI_WEBSOCKET_OPTION_FAILED_FORMAT "xiaozhi websocket option %s failed: %s"

struct ActivationBuffer {
    char data[kActivationResponseSize] = {};
    size_t len = 0;
};

struct VoiceIoBuffers {
    char incoming[kIncomingAudioBufferSize] = {};
    int16_t decode_pcm[2880] = {};
    int16_t playback_pcm[1600] = {};
};

struct VoiceEncodeBuffers {
    int16_t mono[kOpusFrameSamples] = {};
    uint8_t opus[kOpusPacketCapacity] = {};
    uint8_t framed[kOpusPacketCapacity + kWebsocketAudioHeaderCapacity] = {};
};

EventGroupHandle_t s_events = nullptr;
SemaphoreHandle_t s_snapshot_mutex = nullptr;
TaskHandle_t s_task_handle = nullptr;
std::atomic<bool> s_task_exited{true};
std::atomic<bool> s_alarm_suspended{false};
static_assert(kXiaozhiTaskStackSize % sizeof(StackType_t) == 0,
              "Xiaozhi task stack must align to StackType_t");
static_assert(kActivationHttpTimeoutMs > 0 && kBindingVoiceTaskStackBytes > 0,
              "Xiaozhi activation timeout and binding task stack must be positive");
static_assert(kTtsPlaybackStopRetryCount > 0 && kTtsPlaybackStopRetryDelayMs > 0,
              "Xiaozhi TTS stop retry settings must be positive");
static_assert(kTtsFinalFrameGraceMs > kTtsPlaybackTailSettleMs,
              "TTS final-frame grace must exceed playback tail settling time");
// The main AI task reads NVS. Flash/NVS operations temporarily disable the
// external-memory cache, so its stack must stay in internal DRAM. Reserving it
// statically avoids the late 24 KiB contiguous-heap allocation failure.
StackType_t s_task_stack[kXiaozhiTaskStackSize / sizeof(StackType_t)];
StaticTask_t s_task_buffer;
// 固定 char 数组的静态聚合初始化需要字符串字面量；运行期更新仍复用状态常量。
XiaozhiAiSnapshot s_snapshot = {kXiaozhiAiInactive, "小智准备中", "", "", "neutral", 0, 0};
bool s_network_lock_held = false;
bool s_network_keepalive = false;
bool s_idle_low_power = false;
bool s_voice_started = false;
bool s_task_start_attempted = false;
char s_last_announced_binding_code[24] = {};
std::atomic<bool> s_tts_playback_running{false};
std::atomic<bool> s_tts_playback_exited{true};
std::atomic<bool> s_tts_playback_busy{false};
std::atomic<bool> s_tts_playback_failed{false};
TaskHandle_t s_tts_playback_task = nullptr;
StackType_t *s_tts_playback_stack = nullptr;
StaticTask_t *s_tts_playback_task_buffer = nullptr;
uint8_t *s_tts_playback_storage = nullptr;
StaticStreamBuffer_t *s_tts_playback_stream_buffer = nullptr;
StreamBufferHandle_t s_tts_playback_stream = nullptr;

void reclaim_ai_task_if_exited()
{
    if (!s_task_handle || !s_task_exited.load() ||
        eTaskGetState(s_task_handle) != eSuspended) {
        return;
    }
    vTaskDelete(s_task_handle);
    s_task_handle = nullptr;
}

void log_voice_resources(const char *stage)
{
    ESP_LOGI(TAG,
             "xiaozhi resources %s: stack_free=%u dma_free=%u dma_largest=%u "
             "internal_free=%u internal_largest=%u psram_free=%u",
             stage ? stage : "unknown",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

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

struct WebsocketSession {
    esp_transport_handle_t parent = nullptr;
    esp_transport_handle_t socket = nullptr;
    int version = 1;
    int output_sample_rate = 16000;
    char session_id[48] = {};
    bool network_transaction_locked = false;
    bool playback_format_logged = false;
    bool server_speaking = false;
    bool resume_listening_pending = false;
    bool discard_tts_audio = false;
    bool exit_after_reply_requested = false;
    bool exit_reply_started = false;
    bool peer_disconnected = false;
    TickType_t tts_stop_received_tick = 0;
    TickType_t last_tts_audio_tick = 0;
    TickType_t exit_reply_deadline = 0;
    TickType_t user_text_hold_until = 0;
    char pending_assistant_text[192] = {};
};

void snapshot_set(XiaozhiAiState state, const char *status, const char *detail, const char *binding_code = nullptr);
void snapshot_set_status_preserving_detail(XiaozhiAiState state, const char *status);
void snapshot_mark_user_activity();
void snapshot_set_emotion(const char *emotion);
void format_device_id(char *out, size_t out_len);
bool load_or_create_client_id(char *out, size_t out_len);
bool load_websocket_config(char *url, size_t url_len, char *token, size_t token_len, int32_t *version);

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
    auto play_pcm = [](const EmbeddedPcm &pcm) {
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
    };
    bool played = play_pcm(kBindingPromptPcm);
    static const int16_t silence[kBindingPauseSamples] = {};
    if (played) {
        played = write_xiaozhi_speaker(silence,
                                       kBindingPauseSamples,
                                       kBindingPcmSampleRate) == ESP_CODEC_DEV_OK;
    }
    for (const char *cursor = binding_code; played && *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            continue;
        }
        played = play_pcm(kBindingDigitPcm[*cursor - '0']);
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

void announce_binding_id_once(const char *binding_code)
{
    if (!binding_code || binding_code[0] == '\0' || strcmp(binding_code, s_last_announced_binding_code) == 0) {
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
    strlcpy(s_last_announced_binding_code, binding_code, sizeof(s_last_announced_binding_code));
}

void close_websocket(WebsocketSession *session)
{
    if (!session) {
        return;
    }
    bool release_transaction_lock = session->network_transaction_locked;
    if (session->socket) {
        if (!session->peer_disconnected) {
            esp_transport_close(session->socket);
        }
        esp_transport_destroy(session->socket);
    }
    if (session->parent) {
        esp_transport_destroy(session->parent);
    }
    *session = {};
    if (release_transaction_lock) {
        release_network_http_transaction_lock();
    }
}

bool websocket_send_text(WebsocketSession *session, const char *text)
{
    return session && session->socket && text &&
           esp_transport_ws_send_raw(session->socket,
                                     static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN | WS_TRANSPORT_OPCODES_TEXT),
                                     text, strlen(text), kWebsocketTimeoutMs) >= 0;
}

bool websocket_send_listen_start(WebsocketSession *session)
{
    if (!session || session->session_id[0] == '\0') {
        return false;
    }
    char listen[128] = {};
    snprintf(listen,
             sizeof(listen),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"realtime\"}",
             session->session_id);
    return websocket_send_text(session, listen);
}

bool websocket_send_wake_abort(WebsocketSession *session)
{
    if (!session || session->session_id[0] == '\0') {
        return false;
    }
    char message[128] = {};
    snprintf(message,
             sizeof(message),
             "{\"session_id\":\"%s\",\"type\":\"abort\",\"reason\":\"wake_word_detected\"}",
             session->session_id);
    return websocket_send_text(session, message);
}

bool suspend_websocket_transaction_lock(WebsocketSession *session)
{
    if (!session || !session->network_transaction_locked) {
        return false;
    }
    session->network_transaction_locked = false;
    release_network_http_transaction_lock();
    return true;
}

bool restore_websocket_transaction_lock(WebsocketSession *session, bool was_locked)
{
    if (!was_locked) {
        return true;
    }
    if (!session ||
        !acquire_network_http_transaction_lock(pdMS_TO_TICKS(kWebsocketTimeoutMs))) {
        ESP_LOGW(TAG, "Xiaozhi failed to restore WebSocket transaction lock");
        return false;
    }
    session->network_transaction_locked = true;
    return true;
}

bool websocket_option_set(esp_err_t err, const char *name)
{
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGW(TAG, XIAOZHI_WEBSOCKET_OPTION_FAILED_FORMAT, name, esp_err_to_name(err));
    return false;
}

bool open_websocket(WebsocketSession *session, const char *url, const char *token, int version)
{
    if (!session) {
        return false;
    }
    bool secure = false;
    char host[128] = {};
    char path[256] = {};
    int port = 0;
    if (!xiaozhi_protocol::parse_websocket_url(url, &secure, host, sizeof(host), &port, path, sizeof(path))) {
        return false;
    }
    if (!acquire_network_http_transaction_lock(pdMS_TO_TICKS(kWebsocketTimeoutMs))) {
        return false;
    }
    session->network_transaction_locked = true;
    session->parent = secure ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (!session->parent) {
        close_websocket(session);
        return false;
    }
    if (secure) {
        esp_transport_ssl_crt_bundle_attach(session->parent, esp_crt_bundle_attach);
    }
    session->socket = esp_transport_ws_init(session->parent);
    if (!session->socket) {
        close_websocket(session);
        return false;
    }
    char device_id[kDeviceIdSize] = {};
    char client_id[kClientIdSize] = {};
    format_device_id(device_id, sizeof(device_id));
    if (!load_or_create_client_id(client_id, sizeof(client_id))) {
        close_websocket(session);
        return false;
    }
    char headers[192] = {};
    snprintf(headers, sizeof(headers), "Protocol-Version: %d\r\nDevice-Id: %s\r\nClient-Id: %s\r\n", version, device_id, client_id);
    esp_transport_ws_set_path(session->socket, path);
    if (!websocket_option_set(esp_transport_ws_set_user_agent(session->socket, kOfficialUserAgent),
                              "User-Agent") ||
        !websocket_option_set(esp_transport_ws_set_headers(session->socket, headers), "headers")) {
        close_websocket(session);
        return false;
    }
    if (token && token[0] != '\0') {
        char authorization[300] = {};
        snprintf(authorization, sizeof(authorization), "%s%s", strchr(token, ' ') ? "" : "Bearer ", token);
        if (!websocket_option_set(esp_transport_ws_set_auth(session->socket, authorization),
                                  "Authorization")) {
            close_websocket(session);
            return false;
        }
    }
    int connect_result = esp_transport_connect(session->socket, host, port, kWebsocketTimeoutMs);
    int upgrade_status = esp_transport_ws_get_upgrade_request_status(session->socket);
    if (connect_result != 0 ||
        upgrade_status != 101) {
        close_websocket(session);
        return false;
    }
    session->version = version > 0 ? version : 1;
    return true;
}

bool parse_server_hello(WebsocketSession *session, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return false;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *transport = cJSON_GetObjectItem(root, "transport");
    cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
    cJSON *audio_params = cJSON_GetObjectItem(root, "audio_params");
    bool hello = cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0 &&
                 cJSON_IsString(transport) && strcmp(transport->valuestring, "websocket") == 0;
    if (hello && cJSON_IsString(session_id)) {
        strlcpy(session->session_id, session_id->valuestring, sizeof(session->session_id));
    }
    if (hello && cJSON_IsObject(audio_params)) {
        cJSON *rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(rate) && (rate->valueint == 16000 || rate->valueint == 24000)) {
            session->output_sample_rate = rate->valueint;
        }
    }
    cJSON_Delete(root);
    return hello && session->session_id[0] != '\0';
}

bool user_subtitle_hold_active(const WebsocketSession *session)
{
    if (!session || session->user_text_hold_until == 0) {
        return false;
    }
    return static_cast<int32_t>(session->user_text_hold_until - xTaskGetTickCount()) > 0;
}

void publish_pending_assistant_text(WebsocketSession *session)
{
    if (!session || user_subtitle_hold_active(session)) {
        return;
    }
    session->user_text_hold_until = 0;
    if (session->pending_assistant_text[0] == '\0') {
        return;
    }
    char text[sizeof(session->pending_assistant_text)] = {};
    strlcpy(text, session->pending_assistant_text, sizeof(text));
    session->pending_assistant_text[0] = '\0';
    snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, text);
}

bool user_requested_xiaozhi_exit(const char *text)
{
    if (!text || text[0] == '\0') {
        return false;
    }
    constexpr const char *kExplicitExitPhrases[] = {
        "关闭小智",
        "停止小智",
        "退出小智",
        "结束小智",
        "关闭对话",
        "停止对话",
        "退出对话",
        "结束对话",
    };
    for (const char *phrase : kExplicitExitPhrases) {
        if (strstr(text, phrase)) {
            return true;
        }
    }
    constexpr const char *kStandaloneExitCommands[] = {
        "关闭", "关闭。", "关闭！", "关闭？",
        "停止", "停止。", "停止！", "停止？",
        "退出", "退出。", "退出！", "退出？",
        "结束", "结束。", "结束！", "结束？",
        "退下", "退下。", "退下！", "退下？",
        "退下吧", "退下吧。", "退下吧！", "退下吧？",
        "你退下吧", "你退下吧。", "你退下吧！", "你退下吧？",
    };
    // 单独说一个结束动词也视作退出；带“闹钟”等宾语的命令继续交给 MCP。
    for (const char *command : kStandaloneExitCommands) {
        if (strcmp(text, command) == 0) {
            return true;
        }
    }
    return false;
}

void return_from_xiaozhi_to_home()
{
    g_active_work_page = first_enabled_work_page();
    if (s_events) {
        xEventGroupClearBits(s_events, kAiPageActiveBit);
        xEventGroupSetBits(s_events, kAiWakeBit);
    }
    notify_ui_task();
}

void update_incoming_text(WebsocketSession *session, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!session || !root) {
        cJSON_Delete(root);
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *state = cJSON_GetObjectItem(root, "state");
    cJSON *text = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0 && cJSON_IsString(state)) {
        if (strcmp(state->valuestring, "start") == 0) {
            session->server_speaking = true;
            session->exit_reply_started = session->exit_after_reply_requested;
            session->resume_listening_pending = false;
            session->discard_tts_audio = false;
            session->tts_stop_received_tick = 0;
            session->last_tts_audio_tick = 0;
            ESP_LOGI(TAG, "Xiaozhi TTS started");
            if (user_subtitle_hold_active(session)) {
                snapshot_set_status_preserving_detail(kXiaozhiAiSpeaking, kSpeakingStatus);
            } else {
                snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, "直接说话即可打断");
            }
        } else if (strcmp(state->valuestring, "stop") == 0) {
            // Keep the speaker open until any final binary frames already in
            // the WebSocket have been drained. The duplex microphone/AEC
            // stream continues uninterrupted throughout this transition.
            session->server_speaking = true;
            session->resume_listening_pending = true;
            session->tts_stop_received_tick = xTaskGetTickCount();
            ESP_LOGI(TAG,
                     "Xiaozhi TTS stop received: queued=%u busy=%d",
                     s_tts_playback_stream
                         ? static_cast<unsigned>(xStreamBufferBytesAvailable(s_tts_playback_stream))
                         : 0U,
                     s_tts_playback_busy.load() ? 1 : 0);
        } else if (strcmp(state->valuestring, "sentence_start") == 0 && cJSON_IsString(text)) {
            session->server_speaking = true;
            session->exit_reply_started = session->exit_after_reply_requested;
            ESP_LOGI(TAG, "Xiaozhi assistant text (%u bytes): %.160s",
                     static_cast<unsigned>(strlen(text->valuestring)),
                     text->valuestring);
            // The service can emit tool progress markers such as
            // "% get_weather..." as sentence events.  They are not spoken
            // subtitles and look like corrupted text on the compact page.
            if (strncmp(text->valuestring, "% ", 2) != 0) {
                if (user_subtitle_hold_active(session)) {
                    strlcpy(session->pending_assistant_text,
                            text->valuestring,
                            sizeof(session->pending_assistant_text));
                } else {
                    snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, text->valuestring);
                }
            }
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "stt") == 0 && cJSON_IsString(text)) {
        ESP_LOGI(TAG, "Xiaozhi user text (%u bytes): %.160s",
                 static_cast<unsigned>(strlen(text->valuestring)),
                 text->valuestring);
        session->user_text_hold_until =
            xTaskGetTickCount() + pdMS_TO_TICKS(kUserSubtitleMinVisibleMs);
        session->pending_assistant_text[0] = '\0';
        snapshot_set(kXiaozhiAiListening, "正在对话", text->valuestring);
        if (user_requested_xiaozhi_exit(text->valuestring)) {
            session->exit_after_reply_requested = true;
            session->exit_reply_started = false;
            session->exit_reply_deadline =
                xTaskGetTickCount() + pdMS_TO_TICKS(kExitReplyTimeoutMs);
            ESP_LOGI(TAG, "Xiaozhi voice exit requested, waiting for farewell");
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "llm") == 0) {
        cJSON *emotion = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(emotion) && emotion->valuestring) {
            ESP_LOGI(TAG, "Xiaozhi emotion: %.23s", emotion->valuestring);
            snapshot_set_emotion(emotion->valuestring);
        }
    }
    cJSON_Delete(root);
}

void release_tts_playback_storage()
{
    if (s_tts_playback_stream) {
        vStreamBufferDelete(s_tts_playback_stream);
    }
    s_tts_playback_stream = nullptr;
    free(s_tts_playback_storage);
    free(s_tts_playback_stream_buffer);
    free(s_tts_playback_stack);
    free(s_tts_playback_task_buffer);
    s_tts_playback_storage = nullptr;
    s_tts_playback_stream_buffer = nullptr;
    s_tts_playback_stack = nullptr;
    s_tts_playback_task_buffer = nullptr;
}

void tts_playback_task(void *)
{
    int16_t pcm[kTtsPlaybackChunkSamples] = {};
    bool primed = false;
    TickType_t pending_since = 0;
    while (s_tts_playback_running.load()) {
        size_t available = s_tts_playback_stream
                               ? xStreamBufferBytesAvailable(s_tts_playback_stream)
                               : 0;
        if (!primed) {
            if (available == 0) {
                pending_since = 0;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            TickType_t now = xTaskGetTickCount();
            if (pending_since == 0) {
                pending_since = now;
            }
            if (available < kTtsPlaybackPrebufferBytes &&
                (now - pending_since) < pdMS_TO_TICKS(kTtsPlaybackPrebufferWaitMs)) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            primed = true;
        }
        // Mark busy before removing bytes so the network task cannot observe an
        // empty queue and close the speaker in the small receive/write window.
        s_tts_playback_busy.store(true);
        size_t received = xStreamBufferReceive(s_tts_playback_stream,
                                               pcm,
                                               sizeof(pcm),
                                               pdMS_TO_TICKS(100));
        if (received == 0) {
            s_tts_playback_busy.store(false);
            primed = false;
            pending_since = 0;
            continue;
        }
        received -= received % sizeof(int16_t);
        if (received == 0) {
            s_tts_playback_busy.store(false);
            continue;
        }
        int result = write_xiaozhi_speaker(pcm,
                                           received / sizeof(int16_t),
                                           kXiaozhiHardwareSampleRate);
        s_tts_playback_busy.store(false);
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Xiaozhi queued speaker write failed: %d", result);
            s_tts_playback_failed.store(true);
            s_tts_playback_running.store(false);
        }
    }
    s_tts_playback_busy.store(false);
    s_tts_playback_exited.store(true);
    vTaskSuspend(nullptr);
}

void stop_tts_playback()
{
    s_tts_playback_running.store(false);
    for (int retry = 0;
         s_tts_playback_task && !s_tts_playback_exited.load() && retry < kTtsPlaybackStopRetryCount;
         ++retry) {
        vTaskDelay(pdMS_TO_TICKS(kTtsPlaybackStopRetryDelayMs));
    }
    if (s_tts_playback_task) {
        vTaskDelete(s_tts_playback_task);
        s_tts_playback_task = nullptr;
    }
    s_tts_playback_exited.store(true);
    s_tts_playback_busy.store(false);
    release_tts_playback_storage();
}

bool start_tts_playback()
{
    if (s_tts_playback_running.load() && s_tts_playback_task) {
        return true;
    }
    stop_tts_playback();
    s_tts_playback_storage = static_cast<uint8_t *>(heap_caps_calloc(
        1, kTtsPlaybackStreamBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_tts_playback_stream_buffer = static_cast<StaticStreamBuffer_t *>(heap_caps_calloc(
        1, sizeof(StaticStreamBuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    s_tts_playback_stack = static_cast<StackType_t *>(heap_caps_calloc(
        1, kTtsPlaybackTaskStackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_tts_playback_task_buffer = static_cast<StaticTask_t *>(heap_caps_calloc(
        1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!s_tts_playback_storage || !s_tts_playback_stream_buffer ||
        !s_tts_playback_stack || !s_tts_playback_task_buffer) {
        ESP_LOGW(TAG, "Xiaozhi TTS playback storage allocation failed");
        release_tts_playback_storage();
        return false;
    }
    s_tts_playback_stream = xStreamBufferCreateStatic(
        kTtsPlaybackStreamBytes,
        sizeof(int16_t),
        s_tts_playback_storage,
        s_tts_playback_stream_buffer);
    if (!s_tts_playback_stream) {
        ESP_LOGW(TAG, XIAOZHI_TTS_STREAM_CREATE_FAILED_LOG);
        release_tts_playback_storage();
        return false;
    }
    s_tts_playback_failed.store(false);
    s_tts_playback_busy.store(false);
    s_tts_playback_exited.store(false);
    s_tts_playback_running.store(true);
    s_tts_playback_task = xTaskCreateStaticPinnedToCore(
        tts_playback_task,
        "xiaozhi_tts",
        kTtsPlaybackTaskStackBytes,
        nullptr,
        kTtsPlaybackTaskPriority,
        s_tts_playback_stack,
        s_tts_playback_task_buffer,
        1);
    if (!s_tts_playback_task) {
        ESP_LOGW(TAG, XIAOZHI_TTS_TASK_CREATE_FAILED_LOG);
        s_tts_playback_running.store(false);
        s_tts_playback_exited.store(true);
        release_tts_playback_storage();
        return false;
    }
    ESP_LOGI(TAG,
             "Xiaozhi TTS queue ready: psram=%u prebuffer=%u ms",
             static_cast<unsigned>(kTtsPlaybackStreamBytes + kTtsPlaybackTaskStackBytes),
             static_cast<unsigned>(kTtsPlaybackPrebufferWaitMs));
    return true;
}

bool enqueue_tts_playback(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0 || !s_tts_playback_running.load() ||
        !s_tts_playback_stream || s_tts_playback_failed.load()) {
        return false;
    }
    size_t bytes = sample_count * sizeof(int16_t);
    size_t sent = xStreamBufferSend(s_tts_playback_stream,
                                    samples,
                                    bytes,
                                    pdMS_TO_TICKS(250));
    if (sent != bytes) {
        ESP_LOGW(TAG,
                 "Xiaozhi TTS queue full: sent=%u expected=%u free=%u",
                 static_cast<unsigned>(sent),
                 static_cast<unsigned>(bytes),
                 static_cast<unsigned>(xStreamBufferSpacesAvailable(s_tts_playback_stream)));
        return false;
    }
    return true;
}

bool tts_playback_drained()
{
    return s_tts_playback_stream &&
           xStreamBufferBytesAvailable(s_tts_playback_stream) == 0 &&
           !s_tts_playback_busy.load();
}

bool tts_final_frames_settled(const WebsocketSession &session)
{
    TickType_t now = xTaskGetTickCount();
    if (session.tts_stop_received_tick != 0 &&
        now - session.tts_stop_received_tick < pdMS_TO_TICKS(kTtsFinalFrameGraceMs)) {
        return false;
    }
    return session.last_tts_audio_tick == 0 ||
           now - session.last_tts_audio_tick >= pdMS_TO_TICKS(kTtsPlaybackTailSettleMs);
}

bool decode_incoming_audio(WebsocketSession *session,
                           uint8_t *data,
                           size_t len,
                           void *decoder,
                           esp_ae_rate_cvt_handle_t rate_converter,
                           VoiceIoBuffers *buffers)
{
    if (!session || !data || len == 0 || !decoder || !buffers) {
        return false;
    }
    if (session->discard_tts_audio) {
        return true;
    }
    uint8_t *payload = data;
    size_t payload_len = len;
    if (session->version == 2) {
        if (len <= 16) return false;
        payload = data + 16;
        payload_len = len - 16;
    } else if (session->version == 3) {
        if (len <= 4) return false;
        payload = data + 4;
        payload_len = len - 4;
    }
    esp_audio_dec_in_raw_t input = {};
    input.buffer = payload;
    input.len = static_cast<uint32_t>(payload_len);
    esp_audio_dec_out_frame_t output = {};
    output.buffer = reinterpret_cast<uint8_t *>(buffers->decode_pcm);
    output.len = sizeof(buffers->decode_pcm);
    esp_audio_dec_info_t info = {};
    if (esp_opus_dec_decode(decoder, &input, &output, &info) != ESP_AUDIO_ERR_OK || output.decoded_size == 0) {
        return false;
    }
    int source_rate = info.sample_rate ? static_cast<int>(info.sample_rate) : session->output_sample_rate;
    const int16_t *playback_samples = buffers->decode_pcm;
    uint32_t playback_sample_count = output.decoded_size / sizeof(int16_t);
    if (source_rate != kXiaozhiHardwareSampleRate) {
        if (!rate_converter) {
            return false;
        }
        uint32_t converted_sample_count = sizeof(buffers->playback_pcm) / sizeof(buffers->playback_pcm[0]);
        if (esp_ae_rate_cvt_process(rate_converter,
                                    buffers->decode_pcm,
                                    playback_sample_count,
                                    buffers->playback_pcm,
                                    &converted_sample_count) != ESP_AE_ERR_OK ||
            converted_sample_count == 0) {
            return false;
        }
        playback_samples = buffers->playback_pcm;
        playback_sample_count = converted_sample_count;
    }
    if (!session->playback_format_logged) {
        ESP_LOGI(TAG,
                 "TTS audio format: source=%dHz decoded=%u playback=%dHz samples=%u",
                 source_rate,
                 static_cast<unsigned>(output.decoded_size / sizeof(int16_t)),
                 kXiaozhiHardwareSampleRate,
                 static_cast<unsigned>(playback_sample_count));
        session->playback_format_logged = true;
    }
    bool queued = enqueue_tts_playback(playback_samples, playback_sample_count);
    if (queued) {
        session->last_tts_audio_tick = xTaskGetTickCount();
    }
    return queued;
}

bool send_encoded_microphone(WebsocketSession *session,
                             void *encoder,
                             VoiceEncodeBuffers *encode_buffers,
                             int encoder_input_size,
                             int encoder_output_size)
{
    if (!session || !encoder || !encode_buffers ||
        encoder_input_size != static_cast<int>(sizeof(encode_buffers->mono)) ||
        encoder_output_size <= 0 ||
        encoder_output_size > static_cast<int>(sizeof(encode_buffers->opus)) ||
        !xiaozhi_voice_read_processed(encode_buffers->mono,
                                      kOpusFrameSamples,
                                      0)) {
        return false;
    }
    esp_audio_enc_in_frame_t input = {};
    input.buffer = reinterpret_cast<uint8_t *>(encode_buffers->mono);
    input.len = static_cast<uint32_t>(encoder_input_size);
    esp_audio_enc_out_frame_t output = {};
    output.buffer = encode_buffers->opus;
    output.len = static_cast<uint32_t>(encoder_output_size);
    if (esp_opus_enc_process(encoder, &input, &output) != ESP_AUDIO_ERR_OK || output.encoded_bytes == 0) {
        return false;
    }
    if (output.encoded_bytes > sizeof(encode_buffers->opus)) {
        ESP_LOGE(TAG, "Opus packet too large: %u", static_cast<unsigned>(output.encoded_bytes));
        return false;
    }
    const char *payload = reinterpret_cast<const char *>(encode_buffers->opus);
    size_t payload_len = output.encoded_bytes;
    if (session->version == 2) {
        uint16_t protocol_version = htons(static_cast<uint16_t>(session->version));
        uint16_t audio_type = 0;
        uint32_t payload_size = htonl(static_cast<uint32_t>(payload_len));
        memcpy(encode_buffers->framed, &protocol_version, sizeof(protocol_version));
        memcpy(encode_buffers->framed + 2, &audio_type, sizeof(audio_type));
        memset(encode_buffers->framed + 4, 0, 8);
        memcpy(encode_buffers->framed + 12, &payload_size, sizeof(payload_size));
        memcpy(encode_buffers->framed + 16, encode_buffers->opus, payload_len);
        payload = reinterpret_cast<const char *>(encode_buffers->framed);
        payload_len += 16;
    } else if (session->version == 3) {
        encode_buffers->framed[0] = 0;
        encode_buffers->framed[1] = 0;
        uint16_t network_len = htons(static_cast<uint16_t>(payload_len));
        memcpy(encode_buffers->framed + 2, &network_len, sizeof(network_len));
        memcpy(encode_buffers->framed + 4, encode_buffers->opus, payload_len);
        payload = reinterpret_cast<const char *>(encode_buffers->framed);
        payload_len += 4;
    }
    return esp_transport_ws_send_raw(session->socket,
                                     static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN | WS_TRANSPORT_OPCODES_BINARY),
                                     payload, static_cast<int>(payload_len), kWebsocketTimeoutMs) >= 0;
}

bool run_voice_conversation()
{
    // TLS、Opus 与状态刷新都会短时占用内部 DMA 内存。复用现有网络守卫，
    // 在小智会话期间让 RLCD 使用 512 字节分块，避免 SPI 临时缓冲分配失败。
    NetworkDisplayDmaGuard display_guard(true);
    log_voice_resources("before websocket");
    char url[256] = {};
    char token[256] = {};
    int32_t version = 1;
    if (!load_websocket_config(url, sizeof(url), token, sizeof(token), &version)) {
        return false;
    }
    WebsocketSession session = {};
    if (!open_websocket(&session, url, token, version)) {
        log_voice_resources("websocket failed");
        return false;
    }
    log_voice_resources("websocket connected");
    VoiceIoBuffers *buffers = static_cast<VoiceIoBuffers *>(
        heap_caps_calloc(1, sizeof(VoiceIoBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffers) {
        close_websocket(&session);
        return false;
    }
    char hello[192] = {};
    snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"version\":%d,\"features\":{\"mcp\":true},\"transport\":\"websocket\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}", session.version);
    if (!websocket_send_text(&session, hello)) {
        free(buffers);
        close_websocket(&session);
        return false;
    }
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(kWebsocketTimeoutMs);
    while (xTaskGetTickCount() < deadline && session.session_id[0] == '\0') {
        int received = esp_transport_read(session.socket, buffers->incoming, sizeof(buffers->incoming), 500);
        if (received > 0 && esp_transport_ws_get_read_opcode(session.socket) == WS_TRANSPORT_OPCODES_TEXT) {
            parse_server_hello(&session, buffers->incoming, static_cast<size_t>(received));
        }
    }
    if (session.session_id[0] == '\0') {
        free(buffers);
        close_websocket(&session);
        return false;
    }
    if (!websocket_send_listen_start(&session)) {
        free(buffers);
        close_websocket(&session);
        return false;
    }
    esp_opus_enc_config_t encoder_cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    encoder_cfg.sample_rate = ESP_AUDIO_SAMPLE_RATE_16K;
    encoder_cfg.channel = ESP_AUDIO_MONO;
    encoder_cfg.bits_per_sample = ESP_AUDIO_BIT16;
    encoder_cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    encoder_cfg.bitrate = ESP_OPUS_BITRATE_AUTO;
    encoder_cfg.application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
    encoder_cfg.enable_dtx = true;
    encoder_cfg.enable_vbr = true;
    void *encoder = nullptr;
    esp_opus_dec_cfg_t decoder_cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
    decoder_cfg.sample_rate = session.output_sample_rate;
    decoder_cfg.channel = ESP_AUDIO_MONO;
    decoder_cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;
    void *decoder = nullptr;
    esp_ae_rate_cvt_handle_t rate_converter = nullptr;
    bool ready = esp_opus_enc_open(&encoder_cfg, sizeof(encoder_cfg), &encoder) == ESP_AUDIO_ERR_OK &&
                 esp_opus_dec_open(&decoder_cfg, sizeof(decoder_cfg), &decoder) == ESP_AUDIO_ERR_OK;
    if (ready && session.output_sample_rate != kXiaozhiHardwareSampleRate) {
        esp_ae_rate_cvt_cfg_t rate_cfg = {};
        rate_cfg.src_rate = static_cast<uint32_t>(session.output_sample_rate);
        rate_cfg.dest_rate = kXiaozhiHardwareSampleRate;
        rate_cfg.channel = 1;
        rate_cfg.bits_per_sample = 16;
        rate_cfg.complexity = 2;
        rate_cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
        ready = esp_ae_rate_cvt_open(&rate_cfg, &rate_converter) == ESP_AE_ERR_OK && rate_converter != nullptr;
    }
    int encoder_input_size = 0;
    int encoder_output_size = 0;
    if (ready && esp_opus_enc_get_frame_size(encoder, &encoder_input_size, &encoder_output_size) != ESP_AUDIO_ERR_OK) {
        ready = false;
    }
    VoiceEncodeBuffers *encode_buffers = nullptr;
    if (ready &&
        (encoder_input_size != static_cast<int>(sizeof(VoiceEncodeBuffers::mono)) ||
         encoder_output_size <= 0 ||
         encoder_output_size > static_cast<int>(sizeof(VoiceEncodeBuffers::opus)))) {
        ESP_LOGE(TAG,
                 "Unsupported Opus frame sizes: input=%d expected=%u output=%d capacity=%u",
                 encoder_input_size,
                 static_cast<unsigned>(sizeof(VoiceEncodeBuffers::mono)),
                 encoder_output_size,
                 static_cast<unsigned>(sizeof(VoiceEncodeBuffers::opus)));
        ready = false;
    }
    if (ready) {
        encode_buffers = static_cast<VoiceEncodeBuffers *>(
            heap_caps_calloc(1, sizeof(VoiceEncodeBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        ready = encode_buffers != nullptr;
    }
    if (ready) {
        ready = start_tts_playback();
    }
    ESP_LOGI(TAG,
             "Opus frame buffers: input=%d output=%d psram=%s bytes=%u",
             encoder_input_size,
             encoder_output_size,
             encode_buffers ? "ready" : "unavailable",
             static_cast<unsigned>(sizeof(VoiceEncodeBuffers)));
    log_voice_resources(ready ? "opus ready" : "opus failed");
    if (ready) {
        xiaozhi_voice_set_streaming(true);
        snapshot_set(kXiaozhiAiListening, "正在聆听", "请开始说话");
    }
    TickType_t last_activity = xTaskGetTickCount();
    while (ready && (xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0 &&
           (xTaskGetTickCount() - last_activity) < pdMS_TO_TICKS(kConversationIdleTimeoutMs)) {
        publish_pending_assistant_text(&session);
        bool wake_interrupt = !session.peer_disconnected && xiaozhi_voice_take_wake_word();
        if (session.server_speaking && wake_interrupt) {
            bool abort_sent = websocket_send_wake_abort(&session);
            stop_tts_playback();
            abort_xiaozhi_speaker_playback();
            (void)esp_opus_dec_reset(decoder);
            session.server_speaking = false;
            session.resume_listening_pending = false;
            session.discard_tts_audio = true;
            session.tts_stop_received_tick = 0;
            session.last_tts_audio_tick = 0;
            bool listen_sent = websocket_send_listen_start(&session);
            (void)play_xiaozhi_wake_feedback();
            bool playback_restarted = start_tts_playback();
            xiaozhi_voice_set_streaming(true);
            session.user_text_hold_until = 0;
            session.pending_assistant_text[0] = '\0';
            snapshot_set(kXiaozhiAiListening, "已打断", "请继续说话");
            ESP_LOGI(TAG,
                     "Xiaozhi wake interrupt: abort=%d listen=%d playback=%d",
                     abort_sent,
                     listen_sent,
                     playback_restarted);
            if (!playback_restarted) {
                ready = false;
                break;
            }
        }
        // AFE fills its stream asynchronously. Never block the WebSocket
        // receive path waiting for a 60 ms uplink frame; otherwise TTS packets
        // arrive slower than the speaker consumes them and cause underruns.
        if (!session.peer_disconnected &&
            xiaozhi_voice_processed_bytes_available() >=
            static_cast<size_t>(encoder_input_size)) {
            if (!send_encoded_microphone(&session,
                                         encoder,
                                         encode_buffers,
                                         encoder_input_size,
                                         encoder_output_size)) {
                ready = false;
                break;
            }
        }
        int received = 0;
        if (!session.peer_disconnected) {
            constexpr int kReadTimeoutMs = 10;
            received = esp_transport_read(session.socket,
                                          buffers->incoming,
                                          sizeof(buffers->incoming),
                                          kReadTimeoutMs);
        }
        if (received > 0) {
            last_activity = xTaskGetTickCount();
            if (esp_transport_ws_get_read_opcode(session.socket) == WS_TRANSPORT_OPCODES_BINARY) {
                if (!decode_incoming_audio(&session,
                                           reinterpret_cast<uint8_t *>(buffers->incoming),
                                           static_cast<size_t>(received),
                                           decoder,
                                           rate_converter,
                                           buffers)) {
                    ready = false;
                    break;
                }
            } else if (esp_transport_ws_get_read_opcode(session.socket) == WS_TRANSPORT_OPCODES_TEXT) {
                bool weather_city_call = xiaozhi_mcp_message_calls_weather_city(
                    buffers->incoming,
                    static_cast<size_t>(received));
                bool websocket_lock_suspended = false;
                if (weather_city_call) {
                    // The WebSocket owns the global HTTP transaction mutex for
                    // its whole lifetime. A QWeather lookup from the same task
                    // would recursively wait on that non-recursive mutex and
                    // can assert inside FreeRTOS. Pause AEC output as well so
                    // the producer cannot fill its stream while HTTPS blocks.
                    xiaozhi_voice_pause_streaming();
                    websocket_lock_suspended = suspend_websocket_transaction_lock(&session);
                }
                XiaozhiMcpMessageResult mcp_result = xiaozhi_mcp_handle_message(
                    buffers->incoming,
                    static_cast<size_t>(received),
                    session.session_id,
                    buffers->incoming,
                    sizeof(buffers->incoming),
                    !session.exit_after_reply_requested);
                if (weather_city_call) {
                    bool lock_restored = restore_websocket_transaction_lock(
                        &session,
                        websocket_lock_suspended);
                    xiaozhi_voice_set_streaming(true);
                    if (!lock_restored) {
                        ready = false;
                        break;
                    }
                }
                if (mcp_result == kXiaozhiMcpHandledWithResponse) {
                    if (!websocket_send_text(&session, buffers->incoming)) {
                        ready = false;
                        break;
                    }
                } else if (mcp_result == kXiaozhiMcpNotHandled) {
                    update_incoming_text(&session, buffers->incoming, static_cast<size_t>(received));
                }
            }
        } else if (received < 0 && session.exit_after_reply_requested) {
            session.peer_disconnected = true;
            ESP_LOGI(TAG, "Xiaozhi peer closed after farewell; draining local audio");
        } else if (received < 0) {
            ready = false;
            break;
        }
        if (session.exit_after_reply_requested &&
                   session.exit_reply_started &&
                   tts_playback_drained() &&
                   tts_final_frames_settled(session) &&
                   (session.resume_listening_pending || session.peer_disconnected)) {
            ESP_LOGI(TAG, "Xiaozhi farewell played, returning home");
            return_from_xiaozhi_to_home();
            break;
        } else if (!session.peer_disconnected &&
                   session.resume_listening_pending &&
                   tts_playback_drained() &&
                   tts_final_frames_settled(session)) {
            if (weather_city_mcp_save_pending()) {
                ESP_LOGI(TAG, "Xiaozhi weather city reply finished; closing voice session for safe refresh");
                snapshot_set(kXiaozhiAiActivating, "天气城市已设置", "正在后台更新全部天气");
                break;
            }
            if (!resume_xiaozhi_microphone_after_playback() ||
                !websocket_send_listen_start(&session)) {
                ready = false;
                break;
            }
            session.resume_listening_pending = false;
            session.server_speaking = false;
            session.discard_tts_audio = false;
            session.tts_stop_received_tick = 0;
            session.last_tts_audio_tick = 0;
            last_activity = xTaskGetTickCount();
            if (user_subtitle_hold_active(&session)) {
                snapshot_set_status_preserving_detail(kXiaozhiAiListening, "正在聆听");
            } else {
                bool had_pending_subtitle = session.pending_assistant_text[0] != '\0';
                publish_pending_assistant_text(&session);
                if (!had_pending_subtitle) {
                    snapshot_set(kXiaozhiAiListening, "正在聆听", "请继续说话");
                }
            }
            ESP_LOGI(TAG, "Xiaozhi listening resumed for next turn");
        }
        if (session.peer_disconnected) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (session.exit_after_reply_requested &&
            session.exit_reply_deadline != 0 &&
            static_cast<int32_t>(xTaskGetTickCount() - session.exit_reply_deadline) >= 0 &&
            (!session.exit_reply_started || tts_playback_drained())) {
            ESP_LOGW(TAG, "Xiaozhi farewell timeout, returning home");
            return_from_xiaozhi_to_home();
            break;
        }
    }
    stop_tts_playback();
    xiaozhi_voice_set_streaming(false);
    if (encoder) esp_opus_enc_close(encoder);
    if (decoder) esp_opus_dec_close(decoder);
    if (rate_converter) esp_ae_rate_cvt_close(rate_converter);
    free(encode_buffers);
    free(buffers);
    close_websocket(&session);
    log_voice_resources("conversation closed");
    return ready;
}

void snapshot_set(XiaozhiAiState state, const char *status, const char *detail, const char *binding_code)
{
    if (!s_snapshot_mutex || xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_snapshot.state = state;
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.status, sizeof(s_snapshot.status), status);
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.detail, sizeof(s_snapshot.detail), detail);
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.binding_code, sizeof(s_snapshot.binding_code), binding_code);
    if (state != kXiaozhiAiSpeaking) {
        strlcpy(s_snapshot.emotion, kNeutralEmotion, sizeof(s_snapshot.emotion));
    }
    s_snapshot.waveform_level = state == kXiaozhiAiListening || state == kXiaozhiAiSpeaking ? 1 : 0;
    xSemaphoreGive(s_snapshot_mutex);
    notify_ui_task();
}

void snapshot_set_status_preserving_detail(XiaozhiAiState state, const char *status)
{
    if (!s_snapshot_mutex || xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_snapshot.state = state;
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.status, sizeof(s_snapshot.status), status);
    if (state != kXiaozhiAiSpeaking) {
        strlcpy(s_snapshot.emotion, kNeutralEmotion, sizeof(s_snapshot.emotion));
    }
    s_snapshot.waveform_level =
        state == kXiaozhiAiListening || state == kXiaozhiAiSpeaking ? 1 : 0;
    xSemaphoreGive(s_snapshot_mutex);
    notify_ui_task();
}

void snapshot_set_emotion(const char *emotion)
{
    if (!emotion || !s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.emotion, sizeof(s_snapshot.emotion), emotion);
    xSemaphoreGive(s_snapshot_mutex);
    notify_ui_task();
}

void snapshot_mark_user_activity()
{
    if (!s_snapshot_mutex || xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    ++s_snapshot.activity_sequence;
    xSemaphoreGive(s_snapshot_mutex);
    notify_ui_task();
}

bool nvs_read_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len)
{
    if (!key || !xiaozhi_protocol::output_buffer_available(out, out_len)) {
        return false;
    }
    size_t len = out_len;
    if (nvs_get_str(nvs, key, out, &len) != ESP_OK || out[0] == '\0') {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool load_websocket_config(char *url, size_t url_len, char *token, size_t token_len, int32_t *version)
{
    if (!xiaozhi_protocol::output_buffer_available(url, url_len) ||
        !xiaozhi_protocol::output_buffer_available(token, token_len) || !version) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    uint8_t binding_confirmed = 0;
    bool present = nvs_get_u8(nvs, kBindingConfirmedKey, &binding_confirmed) == ESP_OK &&
                   binding_confirmed == 1 &&
                   nvs_read_string(nvs, kWebsocketUrlKey, url, url_len);
    nvs_read_string(nvs, kWebsocketTokenKey, token, token_len);
    if (nvs_get_i32(nvs, kWebsocketVersionKey, version) != ESP_OK || *version <= 0) {
        *version = 1;
    }
    nvs_close(nvs);
    return present;
}

bool save_activation_config(cJSON *websocket, const char *challenge)
{
    if (!cJSON_IsObject(websocket)) {
        return false;
    }
    cJSON *url = cJSON_GetObjectItem(websocket, "url");
    cJSON *token = cJSON_GetObjectItem(websocket, "token");
    cJSON *version = cJSON_GetObjectItem(websocket, "version");
    if (!cJSON_IsString(url) || !url->valuestring || url->valuestring[0] == '\0') {
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_NVS_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(nvs, kWebsocketUrlKey, url->valuestring);
    if (err == ESP_OK && cJSON_IsString(token) && token->valuestring) {
        err = nvs_set_str(nvs, kWebsocketTokenKey, token->valuestring);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, kWebsocketVersionKey, cJSON_IsNumber(version) ? version->valueint : 1);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kBindingConfirmedKey, 1);
    }
    if (err == ESP_OK && challenge && challenge[0] != '\0') {
        err = nvs_set_str(nvs, kActivationChallengeKey, challenge);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_NVS_SAVE_FAILED_FORMAT, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

void format_device_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {};
    if (!xiaozhi_protocol::output_buffer_available(out, out_len) ||
        esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool load_or_create_client_id(char *out, size_t out_len)
{
    if (!xiaozhi_protocol::output_buffer_available(out, out_len) || out_len < kClientIdSize) {
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_CLIENT_ID_NVS_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    size_t stored_len = out_len;
    if (nvs_get_str(nvs, kClientIdKey, out, &stored_len) == ESP_OK && strlen(out) == kClientIdSize - 1) {
        nvs_close(nvs);
        return true;
    }
    uint8_t uuid[16] = {};
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
    snprintf(out,
             out_len,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);
    err = nvs_set_str(nvs, kClientIdKey, out);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_CLIENT_ID_NVS_SAVE_FAILED_FORMAT, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

esp_err_t activation_http_event(esp_http_client_event_t *event)
{
    if (!event || event->event_id != HTTP_EVENT_ON_DATA || !event->user_data || !event->data || event->data_len <= 0) {
        return ESP_OK;
    }
    ActivationBuffer *buffer = static_cast<ActivationBuffer *>(event->user_data);
    size_t room = sizeof(buffer->data) - buffer->len - 1;
    size_t copy_len = static_cast<size_t>(event->data_len) < room ? static_cast<size_t>(event->data_len) : room;
    if (copy_len > 0) {
        memcpy(buffer->data + buffer->len, event->data, copy_len);
        buffer->len += copy_len;
        buffer->data[buffer->len] = '\0';
    }
    return ESP_OK;
}

bool set_activation_http_header(esp_http_client_handle_t client,
                                const char *name,
                                const char *value)
{
    esp_err_t err = esp_http_client_set_header(client, name, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_HEADER_FAILED_FORMAT, name, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool configure_activation_http_request(esp_http_client_handle_t client,
                                       const char *user_agent,
                                       const char *device_id,
                                       const char *client_id,
                                       const char *body)
{
    if (!set_activation_http_header(client, "Content-Type", "application/json") ||
        !set_activation_http_header(client, "Accept-Language", "zh-CN") ||
        !set_activation_http_header(client, "User-Agent", user_agent) ||
        !set_activation_http_header(client, "Activation-Version", "1") ||
        !set_activation_http_header(client, "Device-Id", device_id) ||
        !set_activation_http_header(client, "Client-Id", client_id)) {
        return false;
    }
    esp_err_t err = esp_http_client_set_post_field(client, body, strlen(body));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_BODY_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool request_activation(ActivationBuffer *response)
{
    if (!response) {
        return false;
    }
    char device_id[kDeviceIdSize] = {};
    char client_id[kClientIdSize] = {};
    format_device_id(device_id, sizeof(device_id));
    if (!load_or_create_client_id(client_id, sizeof(client_id))) {
        return false;
    }
    char *body = static_cast<char *>(
        heap_caps_calloc(1, kActivationRequestSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {
        return false;
    }
    uint32_t flash_size = 0;
    (void)esp_flash_get_size(nullptr, &flash_size);
    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    int written = snprintf(
        body,
        kActivationRequestSize,
        "{\"version\":2,\"language\":\"zh-CN\",\"flash_size\":%lu,"
        "\"minimum_free_heap_size\":\"%lu\",\"mac_address\":\"%s\",\"uuid\":\"%s\","
        "\"chip_model_name\":\"esp32s3\",\"chip_info\":{\"model\":%d,\"cores\":%d,\"revision\":%d,\"features\":%lu},"
        "\"application\":{\"name\":\"%s\",\"version\":\"%s\",\"compile_time\":\"%sT%sZ\",\"idf_version\":\"%s\"},"
        "\"ota\":{\"label\":\"%s\"},\"display\":{\"monochrome\":true,\"width\":%d,\"height\":%d},"
        "\"board\":{\"type\":\"wifi\",\"name\":\"s3-rlcd-4.2\",\"mac\":\"%s\"}}",
        static_cast<unsigned long>(flash_size),
        static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
        device_id,
        client_id,
        static_cast<int>(chip_info.model),
        static_cast<int>(chip_info.cores),
        static_cast<int>(chip_info.revision),
        static_cast<unsigned long>(chip_info.features),
        app ? app->project_name : "weather_clock",
        app ? app->version : APP_VERSION,
        app ? app->date : __DATE__,
        app ? app->time : __TIME__,
        app ? app->idf_ver : "unknown",
        running ? running->label : "ota_0",
        kDisplayWidth,
        kDisplayHeight,
        device_id);
    if (written < 0 || static_cast<size_t>(written) >= kActivationRequestSize) {
        free(body);
        return false;
    }
    esp_http_client_config_t config = {};
    config.url = kActivationUrl;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kActivationHttpTimeoutMs;
    config.event_handler = activation_http_event;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    if (!acquire_network_http_transaction_lock(pdMS_TO_TICKS(config.timeout_ms))) {
        ESP_LOGW(TAG, "xiaozhi activation deferred: TLS session is busy");
        free(body);
        return false;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        release_network_http_transaction_lock();
        free(body);
        return false;
    }
    char user_agent[64] = {};
    snprintf(user_agent, sizeof(user_agent), "s3-rlcd-4.2/%s", app ? app->version : APP_VERSION);
    if (!configure_activation_http_request(client, user_agent, device_id, client_id, body)) {
        esp_http_client_cleanup(client);
        release_network_http_transaction_lock();
        free(body);
        return false;
    }
    esp_err_t err = ESP_FAIL;
    {
        NetworkDisplayDmaGuard display_guard(true);
        err = esp_http_client_perform(client);
    }
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    release_network_http_transaction_lock();
    free(body);
    ESP_LOGI(TAG,
             "xiaozhi activation result: status=%d err=%s response_len=%u",
             status,
             esp_err_to_name(err),
             static_cast<unsigned>(response->len));
    return err == ESP_OK && status == 200 && response->len > 0;
}

bool parse_activation_response(const ActivationBuffer &response)
{
    cJSON *root = cJSON_ParseWithLength(response.data, response.len);
    if (!root) {
        return false;
    }
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    cJSON *message = cJSON_IsObject(activation) ? cJSON_GetObjectItem(activation, "message") : nullptr;
    cJSON *code = cJSON_IsObject(activation) ? cJSON_GetObjectItem(activation, "code") : nullptr;
    cJSON *challenge = cJSON_IsObject(activation) ? cJSON_GetObjectItem(activation, "challenge") : nullptr;
    const char *detail = cJSON_IsString(message) && message->valuestring ? message->valuestring : kBindingFallbackDetail;
    const char *binding_code = cJSON_IsString(code) && code->valuestring ? code->valuestring : "";
    const char *challenge_text = cJSON_IsString(challenge) && challenge->valuestring ? challenge->valuestring : "";
    // 未绑定响应可能同时携带 WebSocket 参数和绑定码。绑定码必须优先，
    // 否则把临时连接参数持久化后，下次启动会被误判成已经绑定。
    if (binding_code[0] != '\0') {
        snapshot_set(kXiaozhiAiBinding, kBindingStatus, detail, binding_code);
        announce_binding_id_once(binding_code);
        ESP_LOGI(TAG, "xiaozhi device binding required");
        cJSON_Delete(root);
        return true;
    }
    bool configured = save_activation_config(websocket, challenge_text);
    if (configured) {
        snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        ESP_LOGI(TAG, "xiaozhi device binding confirmed");
    }
    cJSON_Delete(root);
    return configured;
}

void activate_or_restore_session()
{
    char url[256] = {};
    char token[256] = {};
    int32_t version = 1;
    if (load_websocket_config(url, sizeof(url), token, sizeof(token), &version)) {
        snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        return;
    }
    snapshot_set(kXiaozhiAiActivating, kActivatingStatus, "正在请求设备绑定信息");
    ActivationBuffer *response = static_cast<ActivationBuffer *>(
        heap_caps_calloc(1, sizeof(ActivationBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!response) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, "小智内存不足，请稍后重试");
        return;
    }
    bool activated = request_activation(response) && parse_activation_response(*response);
    free(response);
    if (!activated) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, kActivationFailureDetail);
    }
}

void release_realtime_network()
{
    if (s_voice_started || xiaozhi_voice_is_listening()) {
        xiaozhi_voice_stop();
        s_voice_started = false;
    }
    s_idle_low_power = false;
    if (s_network_keepalive) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        s_network_keepalive = false;
    }
    if (s_network_lock_held) {
        release_network_awake_lock();
        s_network_lock_held = false;
    }
    // 页面保活释放后复用既有 Wi-Fi 关闭路径；若天气、诊断或 OTA 仍持有
    // 网络锁，则由对应任务完成后自行关闭，避免抢占正在进行的网络任务。
    if (!network_awake_lock_active()) {
        stop_wifi_radio();
    }
}

void set_idle_low_power(bool enabled)
{
    if (enabled == s_idle_low_power) {
        return;
    }
    if (enabled) {
        if (s_network_lock_held) {
            release_network_awake_lock();
            s_network_lock_held = false;
        }
        esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        if (ps_err != ESP_OK) {
            ESP_LOGW(TAG, "Xiaozhi idle Wi-Fi power save failed: %s", esp_err_to_name(ps_err));
        }
        set_xiaozhi_audio_high_performance(false);
        s_idle_low_power = true;
        ESP_LOGI(TAG, "Xiaozhi wake idle power: CPU DFS + Wi-Fi max modem sleep");
        return;
    }
    if (!s_network_lock_held) {
        acquire_network_awake_lock();
        s_network_lock_held = true;
    }
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "Xiaozhi realtime Wi-Fi power save disable failed: %s", esp_err_to_name(ps_err));
    }
    set_xiaozhi_audio_high_performance(true);
    s_idle_low_power = false;
    ESP_LOGI(TAG, "Xiaozhi realtime power restored");
}

void ensure_wake_word_listening()
{
    if (s_voice_started || xiaozhi_voice_is_listening()) {
        return;
    }
    if (!xiaozhi_voice_start()) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, kWakeWordFailureDetail);
        return;
    }
    s_voice_started = true;
    snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
    set_idle_low_power(true);
}

bool acquire_realtime_network()
{
    bool connected = g_app_events &&
                     ((xEventGroupGetBits(g_app_events) & kWifiConnectedBit) != 0);
    if (s_idle_low_power && g_wifi_radio_on && connected) {
        return true;
    }
    if (s_idle_low_power) {
        set_idle_low_power(false);
    }
    if (!s_network_lock_held) {
        acquire_network_awake_lock();
        s_network_lock_held = true;
    }
    if (!g_wifi_radio_on && !start_wifi_radio(false)) {
        return false;
    }
    // 从这里开始拥有页面级保活；省电模式只在首次取得保活时切换一次。
    // 重复调用 esp_wifi_set_ps() 会刷屏并让状态栏看起来像在反复变化。
    if (!s_network_keepalive) {
        s_network_keepalive = true;
        if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
            ESP_LOGW(TAG, "Xiaozhi Wi-Fi power save disable failed");
        }
    }
    if (!wait_for_wifi_connected(kWifiWaitMs)) {
        return false;
    }
    return true;
}

void xiaozhi_ai_task(void *)
{
    TickType_t next_activation_attempt = 0;
    for (;;) {
        EventBits_t bits = xEventGroupGetBits(s_events);
        bool active = (bits & kAiPageActiveBit) != 0;
        if (!active) {
            release_realtime_network();
            snapshot_set(kXiaozhiAiInactive, kDefaultStatus, "");
            s_task_exited.store(true);
            vTaskSuspend(nullptr);
            return;
        }
        if (g_offline_mode_ui_enabled) {
            release_realtime_network();
            snapshot_set(kXiaozhiAiError, kErrorStatus, kOfflineDetail);
            xEventGroupWaitBits(s_events, kAiWakeBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(kLoopIdleMs));
            continue;
        }
        if (!g_have_wifi_creds) {
            release_realtime_network();
            snapshot_set(kXiaozhiAiWaitingForWifi, kWifiStatus, kNoWifiDetail);
            xEventGroupWaitBits(s_events, kAiWakeBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(kLoopIdleMs));
            continue;
        }
        if (!acquire_realtime_network()) {
            snapshot_set(kXiaozhiAiWaitingForWifi, kWifiStatus, "连接失败，正在重试");
            xEventGroupWaitBits(s_events, kAiWakeBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(kActivationRetryMs));
            continue;
        }
        TickType_t now = xTaskGetTickCount();
        if (next_activation_attempt == 0 || now >= next_activation_attempt) {
            activate_or_restore_session();
            next_activation_attempt = now + pdMS_TO_TICKS(kActivationRetryMs);
        }
        if (s_snapshot.state == kXiaozhiAiReady) {
            ensure_wake_word_listening();
        }
        if (xiaozhi_voice_take_wake_word()) {
            // The audio session stays owned by the existing audio service;
            // protocol I/O cannot create a competing I2S or Wi-Fi stack.
            set_idle_low_power(false);
            snapshot_mark_user_activity();
            (void)play_xiaozhi_wake_feedback();
            snapshot_set(kXiaozhiAiListening, "已唤醒", "正在连接语音会话");
            bool conversation_ok = run_voice_conversation();
            bool weather_city_pending = weather_city_mcp_save_pending();
            if (xiaozhi_mcp_volume_save_pending() ||
                alarm_save_pending() ||
                weather_city_pending) {
                xiaozhi_voice_stop();
                s_voice_started = false;
                if (!xiaozhi_mcp_flush_pending_settings()) {
                    ESP_LOGW(TAG, "xiaozhi MCP volume save failed");
                }
                if (!alarm_flush_pending_save()) {
                    ESP_LOGW(TAG, "xiaozhi MCP alarm save failed");
                }
                bool weather_city_saved = weather_city_mcp_flush_pending_save();
                if (!weather_city_saved) {
                    ESP_LOGW(TAG, "xiaozhi MCP weather city save failed");
                } else if (weather_city_pending) {
                    // Full weather refresh includes current weather, warning,
                    // forecast and air quality. Run it only after WebSocket,
                    // Opus, AEC and Codec resources have been released.
                    snapshot_set(kXiaozhiAiActivating,
                                 "天气城市已保存",
                                 "正在后台更新全部天气");
                    release_realtime_network();
                    while ((xEventGroupGetBits(g_app_events) & kManualWeatherSyncBit) != 0 &&
                           (xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                }
            }
            if (!conversation_ok) {
                xiaozhi_voice_stop();
                s_voice_started = false;
                snapshot_set(kXiaozhiAiError, kErrorStatus, "语音会话中断，稍后重试");
            } else if ((xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0) {
                snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
                set_idle_low_power(true);
            }
        }
        xEventGroupWaitBits(s_events, kAiWakeBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(kLoopIdleMs));
    }
}
} // namespace

void xiaozhi_ai_init()
{
    if (s_events) {
        return;
    }
    s_events = xEventGroupCreate();
    s_snapshot_mutex = xSemaphoreCreateMutex();
    if (!s_events || !s_snapshot_mutex) {
        ESP_LOGW(TAG, "%s", XIAOZHI_STATE_INIT_FAILED_LOG);
        if (s_snapshot_mutex) {
            vSemaphoreDelete(s_snapshot_mutex);
            s_snapshot_mutex = nullptr;
        }
        if (s_events) {
            vEventGroupDelete(s_events);
            s_events = nullptr;
        }
        return;
    }
}

void xiaozhi_ai_set_page_active(bool active)
{
    if (!s_events) {
        return;
    }
    active = active && !s_alarm_suspended.load();
    reclaim_ai_task_if_exited();
    const bool already_active = (xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0;
    // ui_task evaluates the visible-page state every loop.  Do not turn that
    // polling into an event storm: while inactive, repeated wake events kept
    // this task runnable on CPU1 and could starve the UI idle task.
    if (!active) {
        s_task_start_attempted = false;
        if (!already_active) {
            return;
        }
        xEventGroupClearBits(s_events, kAiPageActiveBit);
        xEventGroupSetBits(s_events, kAiWakeBit);
        return;
    }
    if (!already_active) {
        xEventGroupSetBits(s_events, kAiPageActiveBit | kAiWakeBit);
    }
    if (s_task_handle || s_task_start_attempted || network_awake_lock_active()) {
        return;
    }
    s_task_start_attempted = true;
    ESP_LOGI(TAG,
             "Xiaozhi task heap: internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    s_task_exited.store(false);
    s_task_handle = xTaskCreateStaticPinnedToCore(xiaozhi_ai_task,
                                                  "xiaozhi_ai",
                                                  kXiaozhiTaskStackSize,
                                                  nullptr,
                                                  4,
                                                  s_task_stack,
                                                  &s_task_buffer,
                                                  tskNO_AFFINITY);
    if (!s_task_handle) {
        ESP_LOGW(TAG, "Xiaozhi AI task creation failed");
        s_task_exited.store(true);
        s_task_handle = nullptr;
        xEventGroupClearBits(s_events, kAiWakeBit);
        snapshot_set(kXiaozhiAiError, kErrorStatus, "小智任务启动失败");
        return;
    }
    ESP_LOGI(TAG,
             "Xiaozhi AI task ready: stack_internal_static=%u tcb_internal=%u",
             static_cast<unsigned>(kXiaozhiTaskStackSize),
             static_cast<unsigned>(sizeof(StaticTask_t)));
}

bool xiaozhi_ai_page_active()
{
    return s_events && (xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0;
}

bool xiaozhi_ai_network_keepalive_active()
{
    return s_network_keepalive;
}

void xiaozhi_ai_set_alarm_suspended(bool suspended)
{
    if (s_alarm_suspended.exchange(suspended) == suspended) {
        return;
    }
    if (suspended && s_events) {
        xEventGroupClearBits(s_events, kAiPageActiveBit);
        xEventGroupSetBits(s_events, kAiWakeBit);
    }
    // 解除后由 UI 可见页判断恢复，避免闹钟线程替页面管理器决定是否重启小智。
}

void xiaozhi_ai_get_snapshot(XiaozhiAiSnapshot *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!s_snapshot_mutex || xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        out->state = kXiaozhiAiInactive;
        strlcpy(out->status, kDefaultStatus, sizeof(out->status));
        return;
    }
    *out = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
}

void xiaozhi_ai_clear_activation()
{
    release_realtime_network();
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = nvs_erase_all(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_ERASE_FAILED_FORMAT, esp_err_to_name(err));
    } else {
        err = nvs_commit(nvs);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_COMMIT_FAILED_FORMAT, esp_err_to_name(err));
        }
    }
    nvs_close(nvs);
}
