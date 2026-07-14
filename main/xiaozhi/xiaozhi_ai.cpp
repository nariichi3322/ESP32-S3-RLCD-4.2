// 复用本项目网络与电源服务对接小智官方激活和 WebSocket 会话。
#include "xiaozhi_ai.h"

#include "app_tick_time.h"
#include "app_state.h"
#include "alarm_services.h"
#include "audio_services.h"
#include "network_services.h"
#include "network_https_resources.h"
#include "scoped_heap_buffer.h"
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
#include "xiaozhi_snapshot_state.h"
#include "xiaozhi_tts_playback.h"
#include "xiaozhi_voice.h"
#include "xiaozhi_voice_codec.h"
#include "xiaozhi_websocket_session.h"
#include "weather_city_mcp.h"
#include "wifi_radio_state.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_transport.h>
#include <esp_transport_ws.h>
#include <esp_ae_rate_cvt.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>
#include <arpa/inet.h>
#include <atomic>
#include <string.h>

namespace {
using xiaozhi_websocket::WebsocketSession;
using xiaozhi_websocket::close_websocket;
using xiaozhi_websocket::kTimeoutMs;
using xiaozhi_websocket::open_websocket;
using xiaozhi_websocket::parse_server_hello;
using xiaozhi_websocket::restore_websocket_transaction_lock;
using xiaozhi_websocket::suspend_websocket_transaction_lock;
using xiaozhi_websocket::websocket_send_listen_start;
using xiaozhi_websocket::websocket_send_text;
using xiaozhi_websocket::websocket_send_wake_abort;

constexpr uint32_t kWifiWaitMs = 30000;
constexpr uint32_t kActivationRetryMs = 15000;
constexpr uint32_t kLoopIdleMs = 500;
constexpr uint32_t kWakeAudioPerformanceSettleMs = 40;
constexpr uint32_t kConversationIdleTimeoutMs = 30000;
constexpr uint32_t kExitReplyTimeoutMs = 15000;
constexpr int kIncomingAudioBufferSize = 4096;
constexpr int kXiaozhiHardwareSampleRate = 16000;
constexpr uint32_t kUserSubtitleMinVisibleMs = 2200;
constexpr uint32_t kTtsFinalFrameGraceMs = 350;
constexpr uint32_t kTtsPlaybackTailSettleMs = 120;
// 官方实现为 Opus 编解码任务预留 24 KiB。这里的任务还负责 WebSocket
// 协议，因此至少保持相同栈空间，避免进入 SILK 编码器后破坏任务栈。
constexpr uint32_t kXiaozhiTaskStackSize = 24 * 1024;
constexpr const char *kWifiStatus = "正在连接Wi-Fi";
constexpr const char *kActivatingStatus = "正在连接小智服务";
constexpr const char *kBindingStatus = "请绑定设备";
constexpr const char *kReadyStatus = "等待唤醒词";
constexpr const char *kErrorStatus = "小智服务不可用";
constexpr const char *kSpeakingStatus = "小智正在说话";
constexpr const char *kNoWifiDetail = "请先在系统设置中配置 Wi-Fi";
constexpr const char *kOfflineDetail = "离线模式下无法使用小智 AI";
constexpr const char *kBoundDetail = "说出唤醒词即可开始对话";
constexpr const char *kWakeWordFailureDetail = "语音监听初始化失败，请稍后重试";
constexpr const char *kActivationFailureDetail = "稍后将自动重试";
constexpr const char *kBindingFallbackDetail = "请在小智服务中输入绑定 ID";
constexpr const char *kMalformedAudioFrameLog = "Xiaozhi malformed binary audio frame";
constexpr EventBits_t kAiPageActiveBit = BIT0;
constexpr EventBits_t kAiWakeBit = BIT1;
#define XIAOZHI_STATE_INIT_FAILED_LOG "Xiaozhi AI state initialization failed"

struct VoiceIoBuffers {
    char incoming[kIncomingAudioBufferSize] = {};
    int16_t decode_pcm[2880] = {};
    int16_t playback_pcm[1600] = {};
};

EventGroupHandle_t s_events = nullptr;
TaskHandle_t s_task_handle = nullptr;
std::atomic<bool> s_task_exited{true};
std::atomic<bool> s_alarm_suspended{false};
std::atomic<bool> s_network_keepalive{false};
static_assert(kXiaozhiTaskStackSize % sizeof(StackType_t) == 0,
              "Xiaozhi task stack must align to StackType_t");
static_assert(kWakeAudioPerformanceSettleMs > 0,
              "Xiaozhi wake audio performance settle time must be positive");
static_assert(kTtsFinalFrameGraceMs > kTtsPlaybackTailSettleMs,
              "TTS final-frame grace must exceed playback tail settling time");
// The main AI task reads NVS. Flash/NVS operations temporarily disable the
// external-memory cache, so its stack must stay in internal DRAM. Reserving it
// statically avoids the late 24 KiB contiguous-heap allocation failure.
StackType_t s_task_stack[kXiaozhiTaskStackSize / sizeof(StackType_t)];
StaticTask_t s_task_buffer;
bool s_network_lock_held = false;
bool s_idle_low_power = false;
bool s_voice_started = false;
bool s_task_start_attempted = false;

bool network_keepalive_active()
{
    return s_network_keepalive.load(std::memory_order_acquire);
}

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

void clear_tts_timing_state(WebsocketSession &session)
{
    session.tts_started_tick = 0;
    session.tts_started_tick_set = false;
    session.tts_stop_received_tick = 0;
    session.last_tts_audio_tick = 0;
}

void snapshot_set(XiaozhiAiState state,
                  const char *status,
                  const char *detail,
                  const char *binding_code = nullptr)
{
    xiaozhi_snapshot_set(state, status, detail, binding_code);
}

void snapshot_set_status_preserving_detail(XiaozhiAiState state, const char *status)
{
    xiaozhi_snapshot_set_status_preserving_detail(state, status);
}

void snapshot_set_emotion(const char *emotion)
{
    xiaozhi_snapshot_set_emotion(emotion);
}

void snapshot_mark_user_activity()
{
    xiaozhi_snapshot_mark_user_activity();
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
    active_work_page_store(first_enabled_work_page());
    if (s_events) {
        xEventGroupClearBits(s_events, kAiPageActiveBit);
        xEventGroupSetBits(s_events, kAiWakeBit);
    }
    notify_ui_task();
}

void handle_incoming_tts_start(WebsocketSession &session)
{
    session.server_speaking = true;
    session.exit_reply_started = session.exit_after_reply_requested;
    session.resume_listening_pending = false;
    session.discard_tts_audio = false;
    session.empty_reply_continuation_pending = false;
    session.tts_started_tick = xTaskGetTickCount();
    session.tts_started_tick_set = true;
    session.tts_stop_received_tick = 0;
    session.last_tts_audio_tick = 0;
    ESP_LOGI(TAG, "Xiaozhi TTS started");
    if (user_subtitle_hold_active(&session)) {
        snapshot_set_status_preserving_detail(kXiaozhiAiSpeaking, kSpeakingStatus);
    } else {
        snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, "直接说话即可打断");
    }
}

void handle_incoming_tts_stop(WebsocketSession &session)
{
    // Keep the speaker open until any final binary frames already in
    // the WebSocket have been drained. The duplex microphone/AEC
    // stream continues uninterrupted throughout this transition.
    session.server_speaking = true;
    session.resume_listening_pending = true;
    session.tts_stop_received_tick = xTaskGetTickCount();
    XiaozhiTtsPlaybackSnapshot playback = {};
    xiaozhi_tts_playback_get_snapshot(&playback);
    ESP_LOGI(TAG,
             "Xiaozhi TTS stop received: queued=%u busy=%d",
             static_cast<unsigned>(playback.queued_bytes),
             playback.busy ? 1 : 0);
}

void handle_incoming_tts_sentence_start(WebsocketSession &session, const char *text)
{
    session.server_speaking = true;
    session.turn_assistant_text_received = true;
    session.empty_reply_continuation_pending = false;
    session.exit_reply_started = session.exit_after_reply_requested;
    if (!session.tts_started_tick_set) {
        session.tts_started_tick = xTaskGetTickCount();
        session.tts_started_tick_set = true;
    }
    ESP_LOGI(TAG, "Xiaozhi assistant text (%u bytes): %.160s",
             static_cast<unsigned>(strlen(text)),
             text);
    // The service can emit tool progress markers such as
    // "% get_weather..." as sentence events. They are not spoken
    // subtitles and look like corrupted text on the compact page.
    if (strncmp(text, "% ", 2) == 0) {
        return;
    }
    if (user_subtitle_hold_active(&session)) {
        strlcpy(session.pending_assistant_text,
                text,
                sizeof(session.pending_assistant_text));
    } else {
        snapshot_set(kXiaozhiAiSpeaking, kSpeakingStatus, text);
    }
}

void handle_incoming_stt(WebsocketSession &session, const char *text)
{
    ESP_LOGI(TAG, "Xiaozhi user text (%u bytes): %.160s",
             static_cast<unsigned>(strlen(text)),
             text);
    session.user_text_hold_until =
        xTaskGetTickCount() + pdMS_TO_TICKS(kUserSubtitleMinVisibleMs);
    session.user_text_hold_set = true;
    session.pending_assistant_text[0] = '\0';
    session.turn_user_text_received = true;
    session.turn_assistant_text_received = false;
    session.turn_assistant_audio_received = false;
    session.empty_reply_continuation_pending = false;
    snapshot_set(kXiaozhiAiListening, "正在对话", text);
    if (xiaozhi_user_requested_exit(text)) {
        session.exit_after_reply_requested = true;
        session.exit_reply_started = false;
        session.exit_reply_deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(kExitReplyTimeoutMs);
        session.exit_reply_deadline_set = true;
        ESP_LOGI(TAG, "Xiaozhi voice exit requested, waiting for farewell");
    }
}

void handle_incoming_llm_emotion(const char *emotion)
{
    if (!emotion) {
        return;
    }
    ESP_LOGI(TAG, "Xiaozhi emotion: %.23s", emotion);
    snapshot_set_emotion(emotion);
}

void update_incoming_text(WebsocketSession *session, const char *json, size_t len)
{
    XiaozhiIncomingEvent event;
    if (!event.parse(json, len) || !session) {
        return;
    }
    switch (event.type()) {
        case XiaozhiIncomingEventType::kTtsStart:
            handle_incoming_tts_start(*session);
            break;

        case XiaozhiIncomingEventType::kTtsStop:
            handle_incoming_tts_stop(*session);
            break;

        case XiaozhiIncomingEventType::kTtsSentenceStart:
            handle_incoming_tts_sentence_start(*session, event.text());
            break;

        case XiaozhiIncomingEventType::kStt:
            handle_incoming_stt(*session, event.text());
            break;

        case XiaozhiIncomingEventType::kLlm:
            handle_incoming_llm_emotion(event.emotion());
            break;

        case XiaozhiIncomingEventType::kUnknown:
            break;
    }
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
    size_t payload_offset = 0;
    size_t payload_len = 0;
    if (!xiaozhi_protocol::audio_payload_range(session->version,
                                               data,
                                               len,
                                               &payload_offset,
                                               &payload_len)) {
        ESP_LOGW(TAG, "%s", kMalformedAudioFrameLog);
        return false;
    }
    uint8_t *payload = data + payload_offset;
    esp_audio_dec_in_raw_t input = {};
    input.buffer = payload;
    input.len = static_cast<uint32_t>(payload_len);
    esp_audio_dec_out_frame_t output = {};
    output.buffer = reinterpret_cast<uint8_t *>(buffers->decode_pcm);
    output.len = sizeof(buffers->decode_pcm);
    esp_audio_dec_info_t info = {};
    if (esp_opus_dec_decode(decoder, &input, &output, &info) != ESP_AUDIO_ERR_OK ||
        !xiaozhi_protocol::decoded_audio_size_valid(output.decoded_size,
                                                    sizeof(buffers->decode_pcm))) {
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
    bool queued = xiaozhi_tts_playback_enqueue(playback_samples, playback_sample_count);
    if (queued) {
        session->last_tts_audio_tick = xTaskGetTickCount();
        session->turn_assistant_audio_received = true;
        session->empty_reply_continuation_pending = false;
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
                                      kXiaozhiOpusFrameSamples,
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
    size_t framed_len = 0;
    if (!xiaozhi_protocol::audio_frame_size(session->version,
                                            payload_len,
                                            sizeof(encode_buffers->framed),
                                            &framed_len)) {
        ESP_LOGE(TAG,
                 "Opus frame capacity invalid: version=%d payload=%u capacity=%u",
                 session->version,
                 static_cast<unsigned>(payload_len),
                 static_cast<unsigned>(sizeof(encode_buffers->framed)));
        return false;
    }
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
        payload_len = framed_len;
    } else if (session->version == 3) {
        encode_buffers->framed[0] = 0;
        encode_buffers->framed[1] = 0;
        uint16_t network_len = htons(static_cast<uint16_t>(payload_len));
        memcpy(encode_buffers->framed + 2, &network_len, sizeof(network_len));
        memcpy(encode_buffers->framed + 4, encode_buffers->opus, payload_len);
        payload = reinterpret_cast<const char *>(encode_buffers->framed);
        payload_len = framed_len;
    }
    return esp_transport_ws_send_raw(session->socket,
                                     static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN | WS_TRANSPORT_OPCODES_BINARY),
                                     payload,
                                     static_cast<int>(payload_len),
                                     kTimeoutMs) >= 0;
}

bool start_voice_protocol_session(WebsocketSession *session, VoiceIoBuffers *buffers)
{
    if (!session || !buffers) {
        return false;
    }
    char hello[192] = {};
    xiaozhi_protocol::format_client_hello(hello, sizeof(hello), session->version);
    if (!websocket_send_text(session, hello)) {
        return false;
    }
    TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(kTimeoutMs);
    while (app_tick_deadline_pending(xTaskGetTickCount(), deadline) &&
           session->session_id[0] == '\0') {
        int received = esp_transport_read(session->socket,
                                          buffers->incoming,
                                          sizeof(buffers->incoming),
                                          500);
        if (received > 0 &&
            esp_transport_ws_get_read_opcode(session->socket) == WS_TRANSPORT_OPCODES_TEXT) {
            parse_server_hello(session,
                               buffers->incoming,
                               static_cast<size_t>(received));
        }
    }
    return session->session_id[0] != '\0' && websocket_send_listen_start(session);
}

bool handle_incoming_text_frame(WebsocketSession *session,
                                VoiceIoBuffers *buffers,
                                size_t received)
{
    if (!session || !buffers || received == 0) {
        return false;
    }
    bool weather_city_call = xiaozhi_mcp_message_calls_weather_city(
        buffers->incoming,
        received);
    bool websocket_lock_suspended = false;
    if (weather_city_call) {
        // The WebSocket owns the global HTTP transaction mutex for its whole
        // lifetime. Pause AEC and temporarily yield the lock before QWeather.
        xiaozhi_voice_pause_streaming();
        websocket_lock_suspended = suspend_websocket_transaction_lock(session);
    }
    XiaozhiMcpMessageResult mcp_result = xiaozhi_mcp_handle_message(
        buffers->incoming,
        received,
        session->session_id,
        buffers->incoming,
        sizeof(buffers->incoming),
        !session->exit_after_reply_requested);
    if (weather_city_call) {
        bool lock_restored = restore_websocket_transaction_lock(
            session,
            websocket_lock_suspended);
        xiaozhi_voice_set_streaming(true);
        if (!lock_restored) {
            return false;
        }
    }
    if (mcp_result == kXiaozhiMcpHandledWithResponse) {
        return websocket_send_text(session, buffers->incoming);
    }
    if (mcp_result == kXiaozhiMcpNotHandled) {
        update_incoming_text(session, buffers->incoming, received);
    }
    return true;
}

bool handle_wake_interrupt(WebsocketSession &session, VoiceCodecRuntime &codec_runtime)
{
    bool abort_sent = websocket_send_wake_abort(&session);
    xiaozhi_tts_playback_stop();
    abort_xiaozhi_speaker_playback();
    (void)esp_opus_dec_reset(codec_runtime.decoder);
    session.server_speaking = false;
    session.resume_listening_pending = false;
    session.discard_tts_audio = true;
    clear_tts_timing_state(session);
    bool listen_sent = websocket_send_listen_start(&session);
    bool wake_feedback_played = play_xiaozhi_wake_feedback();
    bool playback_restarted = xiaozhi_tts_playback_start();
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
    return wake_feedback_played && playback_restarted;
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
    ScopedHeapBuffer<uint8_t> buffers_storage(
        static_cast<uint8_t *>(heap_caps_calloc(
            1, sizeof(VoiceIoBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        sizeof(VoiceIoBuffers));
    if (!buffers_storage) {
        close_websocket(&session);
        return false;
    }
    VoiceIoBuffers *buffers = reinterpret_cast<VoiceIoBuffers *>(buffers_storage.data());
    if (!start_voice_protocol_session(&session, buffers)) {
        buffers_storage.reset();
        close_websocket(&session);
        return false;
    }
    VoiceCodecRuntime codec_runtime;
    bool ready = codec_runtime.initialize(session.output_sample_rate);
    if (ready) {
        ready = xiaozhi_tts_playback_start();
    }
    ESP_LOGI(TAG,
             "Opus frame buffers: input=%d output=%d psram=%s bytes=%u",
             codec_runtime.encoder_input_size,
             codec_runtime.encoder_output_size,
             codec_runtime.encode_buffers ? "ready" : "unavailable",
             static_cast<unsigned>(sizeof(VoiceEncodeBuffers)));
    log_voice_resources(ready ? "opus ready" : "opus failed");
    if (ready) {
        xiaozhi_voice_set_streaming(true);
        snapshot_set(kXiaozhiAiListening, "正在聆听", "请开始说话");
    }
    TickType_t last_activity = xTaskGetTickCount();
    while (ready && (xEventGroupGetBits(s_events) & kAiPageActiveBit) != 0 &&
           (xTaskGetTickCount() - last_activity) < pdMS_TO_TICKS(kConversationIdleTimeoutMs)) {
        if (session.empty_reply_continuation_pending &&
            app_tick_deadline_reached(xTaskGetTickCount(),
                                      session.empty_reply_continuation_deadline)) {
            ESP_LOGI(TAG, "Xiaozhi empty reply continuation timeout; returning to wake word");
            break;
        }
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
            if (!handle_wake_interrupt(session, codec_runtime)) {
                ready = false;
                break;
            }
        }
        // AFE fills its stream asynchronously. Never block the WebSocket
        // receive path waiting for a 60 ms uplink frame; otherwise TTS packets
        // arrive slower than the speaker consumes them and cause underruns.
        if (!session.peer_disconnected &&
            xiaozhi_voice_processed_bytes_available() >=
            static_cast<size_t>(codec_runtime.encoder_input_size)) {
            if (!send_encoded_microphone(&session,
                                         codec_runtime.encoder,
                                         codec_runtime.encode_buffers,
                                         codec_runtime.encoder_input_size,
                                         codec_runtime.encoder_output_size)) {
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
                                           codec_runtime.decoder,
                                           codec_runtime.rate_converter,
                                           buffers)) {
                    ready = false;
                    break;
                }
            } else if (esp_transport_ws_get_read_opcode(session.socket) == WS_TRANSPORT_OPCODES_TEXT) {
                if (!handle_incoming_text_frame(&session,
                                                buffers,
                                                static_cast<size_t>(received))) {
                    ready = false;
                    break;
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
                   xiaozhi_tts_playback_drained() &&
                   tts_final_frames_settled(session) &&
                   (session.resume_listening_pending || session.peer_disconnected)) {
            ESP_LOGI(TAG, "Xiaozhi farewell played, returning home");
            return_from_xiaozhi_to_home();
            break;
        } else if (!session.peer_disconnected &&
                   session.resume_listening_pending &&
                   xiaozhi_tts_playback_drained() &&
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
            bool empty_reply = xiaozhi_turn_reply_is_empty(
                session.turn_user_text_received,
                session.turn_assistant_text_received,
                session.turn_assistant_audio_received);
            session.resume_listening_pending = false;
            session.server_speaking = false;
            session.discard_tts_audio = false;
            clear_tts_timing_state(session);
            last_activity = xTaskGetTickCount();
            if (empty_reply) {
                session.empty_reply_continuation_pending = true;
                session.empty_reply_continuation_deadline =
                    last_activity + pdMS_TO_TICKS(kXiaozhiEmptyReplyContinuationMs);
                snapshot_set(kXiaozhiAiListening, "没有听完整", "请继续说，或重新说一遍");
                ESP_LOGI(TAG,
                         "Xiaozhi empty reply; continuation window=%u ms",
                         static_cast<unsigned>(kXiaozhiEmptyReplyContinuationMs));
            } else if (user_subtitle_hold_active(&session)) {
                snapshot_set_status_preserving_detail(kXiaozhiAiListening, "正在聆听");
            } else {
                bool had_pending_subtitle = session.pending_assistant_text[0] != '\0';
                publish_pending_assistant_text(&session);
                if (!had_pending_subtitle) {
                    snapshot_set(kXiaozhiAiListening, "正在聆听", "请继续说话");
                }
            }
            session.turn_user_text_received = false;
            session.turn_assistant_text_received = false;
            session.turn_assistant_audio_received = false;
            ESP_LOGI(TAG, "Xiaozhi listening resumed for next turn");
        }
        if (session.peer_disconnected) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (session.exit_after_reply_requested &&
            session.exit_reply_deadline_set &&
            app_tick_deadline_reached(xTaskGetTickCount(), session.exit_reply_deadline) &&
            (!session.exit_reply_started || xiaozhi_tts_playback_drained())) {
            ESP_LOGW(TAG, "Xiaozhi farewell timeout, returning home");
            return_from_xiaozhi_to_home();
            break;
        }
    }
    xiaozhi_tts_playback_stop();
    xiaozhi_voice_set_streaming(false);
    codec_runtime.release();
    buffers_storage.reset();
    close_websocket(&session);
    log_voice_resources("conversation closed");
    return ready;
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
    ScopedHeapBuffer<uint8_t> response_storage(
        static_cast<uint8_t *>(heap_caps_calloc(
            1,
            sizeof(XiaozhiActivationResponse),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        sizeof(XiaozhiActivationResponse));
    if (!response_storage) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, "小智内存不足，请稍后重试");
        return;
    }
    XiaozhiActivationResponse *response =
        reinterpret_cast<XiaozhiActivationResponse *>(response_storage.data());
    bool activated = xiaozhi_request_activation(response) && parse_activation_response(*response);
    response_storage.reset();
    if (!activated) {
        snapshot_set(kXiaozhiAiError, kErrorStatus, kActivationFailureDetail);
    }
}

void stop_voice_session()
{
    xiaozhi_voice_stop();
    s_voice_started = false;
}

void log_xiaozhi_shutdown_snapshot()
{
    XiaozhiVoiceRuntimeSnapshot voice = {};
    xiaozhi_voice_get_runtime_snapshot(&voice);
    XiaozhiTtsPlaybackSnapshot playback = {};
    xiaozhi_tts_playback_get_snapshot(&playback);
    PowerLockDepthSnapshot power = {};
    bool power_snapshot_ready = get_power_lock_depth_snapshot(&power);
    ESP_LOGI(TAG,
             "Xiaozhi shutdown: tts_task=%d tts_run=%d tts_busy=%d "
             "voice_run=%d stream=%d feed=%d detect=%d afe=%d model=%d buffer=%d "
             "audio=%d codec=%d pm_ok=%d pm_net=%d pm_audio=%d pm_wake=%d pm_cpu=%d wifi=%d",
             playback.task_created ? 1 : 0,
             playback.running ? 1 : 0,
             playback.busy ? 1 : 0,
             voice.running ? 1 : 0,
             voice.streaming ? 1 : 0,
             voice.feed_task ? 1 : 0,
             voice.detect_task ? 1 : 0,
             voice.afe ? 1 : 0,
             voice.model ? 1 : 0,
             voice.processed_stream ? 1 : 0,
             is_audio_playing() ? 1 : 0,
             audio_codec_active() ? 1 : 0,
             power_snapshot_ready ? 1 : 0,
             power.network,
             power.audio,
             power.audio_wake,
             power.audio_cpu,
             wifi_radio_on_load() ? 1 : 0);
}

void release_realtime_network()
{
    XiaozhiVoiceRuntimeSnapshot voice_before = {};
    xiaozhi_voice_get_runtime_snapshot(&voice_before);
    XiaozhiTtsPlaybackSnapshot playback_before = {};
    xiaozhi_tts_playback_get_snapshot(&playback_before);
    bool had_xiaozhi_resources =
        s_voice_started || voice_before.running || voice_before.feed_task ||
        voice_before.detect_task || voice_before.afe || voice_before.model ||
        playback_before.task_created || playback_before.running ||
        playback_before.busy || network_keepalive_active() ||
        s_network_lock_held || s_idle_low_power || is_audio_playing() || audio_codec_active();

    // Error paths can clear a high-level state flag before every worker and
    // peripheral has stopped. Both stop functions are idempotent, so page exit
    // always performs the complete cleanup instead of trusting cached flags.
    xiaozhi_tts_playback_stop();
    stop_voice_session();
    s_idle_low_power = false;
    if (network_keepalive_active()) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        s_network_keepalive.store(false, std::memory_order_release);
    }
    if (s_network_lock_held) {
        release_network_awake_lock();
        s_network_lock_held = false;
    }
    // 页面保活释放时可能正好有天气、诊断等任务持锁。登记延后关闭，
    // 让当前任务或最后释放联网锁的任务完成关机，避免 Wi-Fi 永久漏关。
    request_wifi_radio_stop_when_idle();
    service_wifi_radio_stop_when_idle();
    if (had_xiaozhi_resources) {
        log_xiaozhi_shutdown_snapshot();
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
    if (s_idle_low_power && wifi_radio_on_load() && connected) {
        return true;
    }
    if (s_idle_low_power) {
        set_idle_low_power(false);
    }
    if (!s_network_lock_held) {
        acquire_network_awake_lock();
        s_network_lock_held = true;
    }
    if (!wifi_radio_on_load() && !start_wifi_radio(false)) {
        return false;
    }
    // 从这里开始拥有页面级保活；省电模式只在首次取得保活时切换一次。
    // 重复调用 esp_wifi_set_ps() 会刷屏并让状态栏看起来像在反复变化。
    if (!network_keepalive_active()) {
        s_network_keepalive.store(true, std::memory_order_release);
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
            snapshot_set(kXiaozhiAiInactive, kXiaozhiDefaultStatus, "");
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
        XiaozhiAiSnapshot current_snapshot = {};
        xiaozhi_snapshot_get(&current_snapshot);
        if (current_snapshot.state == kXiaozhiAiReady) {
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
    bool snapshot_ready = xiaozhi_snapshot_state_init();
    if (!s_events || !snapshot_ready) {
        ESP_LOGW(TAG, "%s", XIAOZHI_STATE_INIT_FAILED_LOG);
        xiaozhi_snapshot_state_deinit();
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
    return network_keepalive_active();
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
    xiaozhi_snapshot_get(out);
}

void xiaozhi_ai_clear_activation()
{
    release_realtime_network();
    (void)xiaozhi_clear_activation_storage();
}
