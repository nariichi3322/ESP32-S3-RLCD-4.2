// 获取、筛选和缓存图片时钟底部每日文字。
#include "network_services.h"

#include "daily_saying_parser.h"
#include "ui_views.h"

#define DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT "daily saying response alloc failed"
#define DAILY_SAYING_HTTP_FAILED_LOG_FORMAT "daily saying http failed err=%s"
#define DAILY_SAYING_PARSE_FAILED_LOG_FORMAT "daily saying parse failed"
#define DAILY_SAYING_TOO_LONG_LOG_FORMAT "daily saying too long chars=%d attempt=%d"
#define DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT "daily saying update failed attempts=%d http=%d parse=%d long=%d"
#define DAILY_SAYING_UPDATED_LOG_FORMAT "daily saying updated"

namespace {
constexpr size_t kDailySayingResponseBufferSize = 768;
constexpr int kMaxSayingAttempts = 8;
constexpr const char *const kDailySayingLogTexts[] = {
    DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT,
    DAILY_SAYING_HTTP_FAILED_LOG_FORMAT,
    DAILY_SAYING_PARSE_FAILED_LOG_FORMAT,
    DAILY_SAYING_TOO_LONG_LOG_FORMAT,
    DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT,
    DAILY_SAYING_UPDATED_LOG_FORMAT,
};
portMUX_TYPE s_daily_saying_mux = portMUX_INITIALIZER_UNLOCKED;

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr bool output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

static_assert(array_count(kDailySayingLogTexts) > 0,
              "daily saying log guard must cover log text");
static_assert(cstr_array_nonempty(kDailySayingLogTexts), "daily saying log texts must be non-empty");
static_assert(kDailySayingResponseBufferSize > 0, "daily saying response buffer must be nonzero");
static_assert(kDailySayingResponseBufferSize >= kDailySayingLen,
              "daily saying response buffer must cover cached saying text");
static_assert(kDailySayingLen > daily_saying_parser::kMaxChars,
              "daily saying cache must exceed the accepted character limit plus terminator");
static_assert(kMaxSayingAttempts > 0, "daily saying retry count must be positive");
static_assert(cstr_nonempty(kDailySayingUrl), "daily saying URL must be non-empty");

class DailySayingResponseBuffer {
public:
    DailySayingResponseBuffer()
        : data_((char *)calloc(kDailySayingResponseBufferSize, 1)),
          size_(kDailySayingResponseBufferSize)
    {
        if (!data_) {
            ESP_LOGW(TAG, DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT);
        }
    }

    ~DailySayingResponseBuffer()
    {
        free(data_);
    }

    DailySayingResponseBuffer(const DailySayingResponseBuffer &) = delete;
    DailySayingResponseBuffer &operator=(const DailySayingResponseBuffer &) = delete;

    char *get() const
    {
        return data_;
    }

    void clear() const
    {
        if (data_) {
            memset(data_, 0, size_);
        }
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

private:
    char *data_;
    size_t size_;
};

struct DailySayingAttemptStats {
    int http_failures = 0;
    int parse_failures = 0;
    int long_responses = 0;

    void record_http_failure()
    {
        ++http_failures;
    }

    void record_parse_failure()
    {
        ++parse_failures;
    }

    void record_long_response()
    {
        ++long_responses;
    }
};

void log_daily_saying_update_failed(const DailySayingAttemptStats &stats)
{
    ESP_LOGW(TAG,
             DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT,
             kMaxSayingAttempts,
             stats.http_failures,
             stats.parse_failures,
             stats.long_responses);
}

bool parse_daily_saying_attempt(const char *response,
                                char *out,
                                size_t out_len,
                                int attempt,
                                DailySayingAttemptStats &stats)
{
    bool ok = daily_saying_parser::extract(response, out, out_len);
    if (!ok) {
        stats.record_parse_failure();
        ESP_LOGW(TAG, DAILY_SAYING_PARSE_FAILED_LOG_FORMAT);
        return false;
    }
    int chars = 0;
    if (daily_saying_parser::within_length(out, &chars)) {
        return true;
    }
    stats.record_long_response();
    ESP_LOGW(TAG, DAILY_SAYING_TOO_LONG_LOG_FORMAT, chars, attempt);
    out[0] = '\0';
    return false;
}
} // namespace

void load_daily_saying_cache()
{
    portENTER_CRITICAL(&s_daily_saying_mux);
    g_daily_saying[0] = '\0';
    g_last_saying_sync_time = 0;
    portEXIT_CRITICAL(&s_daily_saying_mux);
}

bool get_daily_saying_snapshot(char *out, size_t out_len, time_t *last_sync_time)
{
    if (!output_buffer_available(out, out_len)) {
        return false;
    }
    portENTER_CRITICAL(&s_daily_saying_mux);
    strlcpy(out, g_daily_saying, out_len);
    if (last_sync_time) {
        *last_sync_time = g_last_saying_sync_time;
    }
    bool available = out[0] != '\0';
    portEXIT_CRITICAL(&s_daily_saying_mux);
    return available;
}

static void publish_daily_saying(const char *text, time_t synced_at)
{
    portENTER_CRITICAL(&s_daily_saying_mux);
    strlcpy(g_daily_saying, text, sizeof(g_daily_saying));
    g_last_saying_sync_time = synced_at;
    portEXIT_CRITICAL(&s_daily_saying_mux);
}

bool perform_daily_saying_update()
{
    if (g_low_battery_mode) {
        return false;
    }
    char next[kDailySayingLen] = {};
    DailySayingResponseBuffer response;
    if (!response) {
        return false;
    }
    DailySayingAttemptStats stats;
    for (int attempt = 1; attempt <= kMaxSayingAttempts; ++attempt) {
        response.clear();
        esp_err_t err = http_get_text(kDailySayingUrl, response.get(), response.size(), nullptr);
        if (err != ESP_OK) {
            stats.record_http_failure();
            ESP_LOGW(TAG, DAILY_SAYING_HTTP_FAILED_LOG_FORMAT, esp_err_to_name(err));
            continue;
        }
        if (parse_daily_saying_attempt(response.get(), next, sizeof(next), attempt, stats)) {
            break;
        }
    }
    if (next[0] == '\0') {
        log_daily_saying_update_failed(stats);
        return false;
    }
    time_t synced_at = 0;
    time(&synced_at);
    publish_daily_saying(next, synced_at);
    notify_ui_task();
    ESP_LOGI(TAG, DAILY_SAYING_UPDATED_LOG_FORMAT);
    return true;
}
