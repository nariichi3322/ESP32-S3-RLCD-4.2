// 复用本项目网络与电源服务对接小智官方激活和 WebSocket 会话。
#include "xiaozhi_ai.h"

#include "app_tick_time.h"
#include "app_state.h"
#include "alarm_services.h"
#include "audio_services.h"
#include "network_services.h"
#include "sensor_services.h"
#include "ui_views.h"
#include "xiaozhi_activation_client.h"
#include "xiaozhi_activation_response_parser.h"
#include "xiaozhi_activation_storage.h"
#include "xiaozhi_binding_voice.h"
#include "xiaozhi_incoming_event_parser.h"
#include "xiaozhi_json_owner.h"
#include "xiaozhi_mcp.h"
#include "xiaozhi_conversation_policy.h"
#include "xiaozhi_protocol_utils.h"
#include "xiaozhi_server_hello_parser.h"
#include "xiaozhi_voice.h"
#include "weather_city_mcp.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ws.h>
#include <esp_ae_rate_cvt.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>
#include <freertos/stream_buffer.h>
#include <arpa/inet.h>
#include <atomic>
#include <string.h>

namespace {
constexpr uint32_t kWifiWaitMs = 30000;
constexpr uint32_t kActivationRetryMs = 15000;
constexpr uint32_t kLoopIdleMs = 500;
constexpr uint32_t kWakeAudioPerformanceSettleMs = 40;
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
constexpr int kTtsPlaybackStopRetryCount = 200;
constexpr uint32_t kTtsPlaybackStopRetryDelayMs = 10;
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
#define XIAOZHI_TTS_STREAM_CREATE_FAILED_LOG "xiaozhi TTS stream buffer creation failed"
#define XIAOZHI_TTS_TASK_CREATE_FAILED_LOG "xiaozhi TTS playback task creation failed"
#define XIAOZHI_STATE_INIT_FAILED_LOG "Xiaozhi AI state initialization failed"
#define XIAOZHI_WEBSOCKET_OPTION_FAILED_FORMAT "xiaozhi websocket option %s failed: %s"

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
static_assert(kWakeAudioPerformanceSettleMs > 0,
              "Xiaozhi wake audio performance settle time must be positive");
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
    bool exit_reply_deadline_set = false;
    bool user_text_hold_set = false;
    bool peer_disconnected = false;
    bool tts_started_tick_set = false;
    TickType_t tts_stop_received_tick = 0;
    TickType_t last_tts_audio_tick = 0;
    TickType_t tts_started_tick = 0;
    TickType_t exit_reply_deadline = 0;
    TickType_t user_text_hold_until = 0;
    char pending_assistant_text[192] = {};
};

void clear_tts_timing_state(WebsocketSession &session)
{
    session.tts_started_tick = 0;
    session.tts_started_tick_set = false;
    session.tts_stop_received_tick = 0;
    session.last_tts_audio_tick = 0;
}

void snapshot_set(XiaozhiAiState state, const char *status, const char *detail, const char *binding_code = nullptr);
void snapshot_set_status_preserving_detail(XiaozhiAiState state, const char *status);
void snapshot_mark_user_activity();
void snapshot_set_emotion(const char *emotion);

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
    xiaozhi_protocol::format_listen_start(listen, sizeof(listen), session->session_id);
    return websocket_send_text(session, listen);
}

bool websocket_send_wake_abort(WebsocketSession *session)
{
    if (!session || session->session_id[0] == '\0') {
        return false;
    }
    char message[128] = {};
    xiaozhi_protocol::format_wake_abort(message, sizeof(message), session->session_id);
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
    char device_id[kXiaozhiDeviceIdSize] = {};
    char client_id[kXiaozhiClientIdSize] = {};
    xiaozhi_format_device_id(device_id, sizeof(device_id));
    if (!xiaozhi_load_or_create_client_id(client_id, sizeof(client_id))) {
        close_websocket(session);
        return false;
    }
    char headers[192] = {};
    xiaozhi_protocol::format_websocket_headers(headers,
                                               sizeof(headers),
                                               version,
                                               device_id,
                                               client_id);
    esp_transport_ws_set_path(session->socket, path);
    if (!websocket_option_set(esp_transport_ws_set_user_agent(session->socket, kOfficialUserAgent),
                              "User-Agent") ||
        !websocket_option_set(esp_transport_ws_set_headers(session->socket, headers), "headers")) {
        close_websocket(session);
        return false;
    }
    if (token && token[0] != '\0') {
        char authorization[300] = {};
        xiaozhi_protocol::format_websocket_authorization(authorization,
                                                         sizeof(authorization),
                                                         token);
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
    return parse_xiaozhi_server_hello(json,
                                      len,
                                      session->session_id,
                                      sizeof(session->session_id),
                                      &session->output_sample_rate);
}

bool user_subtitle_hold_active(const WebsocketSession *session)
{
    if (!session || !session->user_text_hold_set) {
        return false;
    }
    return app_tick_deadline_pending(xTaskGetTickCount(), session->user_text_hold_until);
}

void publish_pending_assistant_text(WebsocketSession *session)
{
    if (!session || user_subtitle_hold_active(session)) {
        return;
    }
    session->user_text_hold_set = false;
    session->user_text_hold_until = 0;
    if (session->pending_assistant_text[0] == '\0') {
        return;
    }
    char text[sizeof(session->pending_assistant_text)] = {};
    strlcpy(text, session->pending_assistant_text, sizeof(text));
    session->pending_assistant_text[0] = '\0';
    snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, text);
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
    XiaozhiIncomingEvent event;
    if (!event.parse(json, len) || !session) {
        return;
    }
    switch (event.type()) {
        case XiaozhiIncomingEventType::kTtsStart:
            session->server_speaking = true;
            session->exit_reply_started = session->exit_after_reply_requested;
            session->resume_listening_pending = false;
            session->discard_tts_audio = false;
            session->tts_started_tick = xTaskGetTickCount();
            session->tts_started_tick_set = true;
            session->tts_stop_received_tick = 0;
            session->last_tts_audio_tick = 0;
            ESP_LOGI(TAG, "Xiaozhi TTS started");
            if (user_subtitle_hold_active(session)) {
                snapshot_set_status_preserving_detail(kXiaozhiAiSpeaking, kSpeakingStatus);
            } else {
                snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, "直接说话即可打断");
            }
            break;

        case XiaozhiIncomingEventType::kTtsStop:
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
            break;

        case XiaozhiIncomingEventType::kTtsSentenceStart:
            session->server_speaking = true;
            session->exit_reply_started = session->exit_after_reply_requested;
            if (!session->tts_started_tick_set) {
                session->tts_started_tick = xTaskGetTickCount();
                session->tts_started_tick_set = true;
            }
            ESP_LOGI(TAG, "Xiaozhi assistant text (%u bytes): %.160s",
                     static_cast<unsigned>(strlen(event.text())),
                     event.text());
            // The service can emit tool progress markers such as
            // "% get_weather..." as sentence events.  They are not spoken
            // subtitles and look like corrupted text on the compact page.
            if (strncmp(event.text(), "% ", 2) != 0) {
                if (user_subtitle_hold_active(session)) {
                    strlcpy(session->pending_assistant_text,
                            event.text(),
                            sizeof(session->pending_assistant_text));
                } else {
                    snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, event.text());
                }
            }
            break;

        case XiaozhiIncomingEventType::kStt:
            ESP_LOGI(TAG, "Xiaozhi user text (%u bytes): %.160s",
                     static_cast<unsigned>(strlen(event.text())),
                     event.text());
            session->user_text_hold_until =
                xTaskGetTickCount() + pdMS_TO_TICKS(kUserSubtitleMinVisibleMs);
            session->user_text_hold_set = true;
            session->pending_assistant_text[0] = '\0';
            snapshot_set(kXiaozhiAiListening, "正在对话", event.text());
            if (xiaozhi_user_requested_exit(event.text())) {
                session->exit_after_reply_requested = true;
                session->exit_reply_started = false;
                session->exit_reply_deadline =
                    xTaskGetTickCount() + pdMS_TO_TICKS(kExitReplyTimeoutMs);
                session->exit_reply_deadline_set = true;
                ESP_LOGI(TAG, "Xiaozhi voice exit requested, waiting for farewell");
            }
            break;

        case XiaozhiIncomingEventType::kLlm:
            if (event.emotion()) {
                ESP_LOGI(TAG, "Xiaozhi emotion: %.23s", event.emotion());
                snapshot_set_emotion(event.emotion());
            }
            break;

        case XiaozhiIncomingEventType::kUnknown:
            break;
    }
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
    if (!xiaozhi_load_websocket_config(url, sizeof(url), token, sizeof(token), &version)) {
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
    xiaozhi_protocol::format_client_hello(hello, sizeof(hello), session.version);
    if (!websocket_send_text(&session, hello)) {
        free(buffers);
        close_websocket(&session);
        return false;
    }
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(kWebsocketTimeoutMs);
    while (app_tick_deadline_pending(xTaskGetTickCount(), deadline) &&
           session.session_id[0] == '\0') {
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
        bool wake_detected = !session.peer_disconnected && xiaozhi_voice_take_wake_word();
        TickType_t wake_tick = xTaskGetTickCount();
        uint32_t speaking_elapsed_ms = session.tts_started_tick_set
                                           ? static_cast<uint32_t>(
                                                 (static_cast<uint64_t>(wake_tick - session.tts_started_tick) *
                                                  1000U) /
                                                 configTICK_RATE_HZ)
                                           : 0U;
        bool wake_interrupt = wake_detected &&
                              xiaozhi_wake_interrupt_allowed(
                                  session.server_speaking,
                                  session.resume_listening_pending,
                                  session.tts_started_tick_set,
                                  speaking_elapsed_ms);
        if (wake_detected && session.server_speaking && !wake_interrupt) {
            ESP_LOGI(TAG,
                     "Xiaozhi wake ignored during TTS guard: elapsed=%u stop_pending=%d",
                     static_cast<unsigned>(speaking_elapsed_ms),
                     session.resume_listening_pending ? 1 : 0);
        }
        if (wake_interrupt) {
            bool abort_sent = websocket_send_wake_abort(&session);
            stop_tts_playback();
            abort_xiaozhi_speaker_playback();
            (void)esp_opus_dec_reset(decoder);
            session.server_speaking = false;
            session.resume_listening_pending = false;
            session.discard_tts_audio = true;
            clear_tts_timing_state(session);
            bool listen_sent = websocket_send_listen_start(&session);
            bool wake_feedback_played = play_xiaozhi_wake_feedback();
            bool playback_restarted = start_tts_playback();
            xiaozhi_voice_set_streaming(true);
            session.user_text_hold_until = 0;
            session.user_text_hold_set = false;
            session.pending_assistant_text[0] = '\0';
            snapshot_set(kXiaozhiAiListening, "已打断", "请继续说话");
            ESP_LOGI(TAG,
                     "Xiaozhi wake interrupt: abort=%d listen=%d feedback=%d playback=%d",
                     abort_sent,
                     listen_sent,
                     wake_feedback_played,
                     playback_restarted);
            if (!wake_feedback_played || !playback_restarted) {
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
            clear_tts_timing_state(session);
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
            session.exit_reply_deadline_set &&
            app_tick_deadline_reached(xTaskGetTickCount(), session.exit_reply_deadline) &&
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

bool parse_activation_response(const XiaozhiActivationResponse &response)
{
    XiaozhiActivationResponseDocument document;
    if (!document.parse(response.data, response.len)) {
        return false;
    }
    const char *detail = document.message() ? document.message() : kBindingFallbackDetail;
    const char *binding_code = document.binding_code() ? document.binding_code() : "";
    const char *challenge_text = document.challenge() ? document.challenge() : "";
    // 未绑定响应可能同时携带 WebSocket 参数和绑定码。绑定码必须优先，
    // 否则把临时连接参数持久化后，下次启动会被误判成已经绑定。
    if (binding_code[0] != '\0') {
        snapshot_set(kXiaozhiAiBinding, kBindingStatus, detail, binding_code);
        xiaozhi_announce_binding_id_once(binding_code);
        ESP_LOGI(TAG, "xiaozhi device binding required");
        return true;
    }
    bool configured = xiaozhi_save_activation_config(document.websocket(), challenge_text);
    if (configured) {
        snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        ESP_LOGI(TAG, "xiaozhi device binding confirmed");
    }
    return configured;
}

void activate_or_restore_session()
{
    char url[256] = {};
    char token[256] = {};
    int32_t version = 1;
    if (xiaozhi_load_websocket_config(url, sizeof(url), token, sizeof(token), &version)) {
        snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        return;
    }
    snapshot_set(kXiaozhiAiActivating, kActivatingStatus, "正在请求设备绑定信息");
    XiaozhiActivationResponse *response = static_cast<XiaozhiActivationResponse *>(
        heap_caps_calloc(1,
                         sizeof(XiaozhiActivationResponse),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!response) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, "小智内存不足，请稍后重试");
        return;
    }
    bool activated = xiaozhi_request_activation(response) && parse_activation_response(*response);
    free(response);
    if (!activated) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, kActivationFailureDetail);
    }
}

void stop_voice_session()
{
    xiaozhi_voice_stop();
    s_voice_started = false;
}

void release_realtime_network()
{
    if (s_voice_started || xiaozhi_voice_is_listening()) {
        stop_voice_session();
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
    bool activation_attempt_scheduled = false;
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
        if (!activation_attempt_scheduled ||
            app_tick_deadline_reached(now, next_activation_attempt)) {
            activate_or_restore_session();
            next_activation_attempt = now + pdMS_TO_TICKS(kActivationRetryMs);
            activation_attempt_scheduled = true;
        }
        if (s_snapshot.state == kXiaozhiAiReady) {
            ensure_wake_word_listening();
        }
        if (xiaozhi_voice_take_wake_word()) {
            // The audio session stays owned by the existing audio service;
            // protocol I/O cannot create a competing I2S or Wi-Fi stack.
            set_idle_low_power(false);
            // 待唤醒阶段会释放 CPU MAX 锁。恢复实时模式后给 APB/I2S/PA
            // 一个短稳定窗口，再打开扬声器，避免首段提示音偶发失真。
            vTaskDelay(pdMS_TO_TICKS(kWakeAudioPerformanceSettleMs));
            snapshot_mark_user_activity();
            if (!play_xiaozhi_wake_feedback()) {
                ESP_LOGW(TAG, "Xiaozhi wake feedback failed; rebuilding voice session");
                stop_voice_session();
                snapshot_set(kXiaozhiAiError, kErrorStatus, "音频状态异常，正在重试");
                continue;
            }
            snapshot_set(kXiaozhiAiListening, "已唤醒", "正在连接语音会话");
            bool conversation_ok = run_voice_conversation();
            bool weather_city_pending = weather_city_mcp_save_pending();
            if (xiaozhi_mcp_volume_save_pending() ||
                alarm_save_pending() ||
                weather_city_pending) {
                stop_voice_session();
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
                stop_voice_session();
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
    (void)xiaozhi_clear_activation_storage();
}
