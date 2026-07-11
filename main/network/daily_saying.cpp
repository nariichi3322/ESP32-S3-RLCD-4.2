// 获取、解析和缓存图片时钟底部每日文字。
#include "network_services.h"

#include "ui_views.h"

#define DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT "daily saying response alloc failed"
#define DAILY_SAYING_HTTP_FAILED_LOG_FORMAT "daily saying http failed err=%s"
#define DAILY_SAYING_PARSE_FAILED_LOG_FORMAT "daily saying parse failed"
#define DAILY_SAYING_TOO_LONG_LOG_FORMAT "daily saying too long chars=%d attempt=%d"
#define DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT "daily saying update failed attempts=%d http=%d parse=%d long=%d"
#define DAILY_SAYING_UPDATED_LOG_FORMAT "daily saying updated"

namespace {
constexpr size_t kDailySayingResponseBufferSize = 768;
constexpr int kMaxSayingChars = 22;
constexpr int kMaxSayingAttempts = 8;
constexpr int kMaxSayingJsonDepth = 8;
constexpr unsigned char kUtf8ContinuationMask = 0xC0;
constexpr unsigned char kUtf8ContinuationPrefix = 0x80;
constexpr char kJsonObjectStart = '{';
constexpr char kJsonArrayStart = '[';
constexpr const char *const kDailySayingJsonFields[] = {
    "content",
    "saying",
    "text",
    "sentence",
    "hitokoto",
    "quote",
    "data",
};
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

constexpr bool daily_saying_json_fields_nonempty()
{
    for (const char *field : kDailySayingJsonFields) {
        if (!cstr_nonempty(field)) {
            return false;
        }
    }
    return true;
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

static_assert(array_count(kDailySayingJsonFields) > 0,
              "daily saying JSON field table must not be empty");
static_assert(daily_saying_json_fields_nonempty(), "daily saying JSON fields must be non-empty");
static_assert(array_count(kDailySayingLogTexts) > 0,
              "daily saying log guard must cover log text");
static_assert(cstr_array_nonempty(kDailySayingLogTexts), "daily saying log texts must be non-empty");
static_assert(kDailySayingResponseBufferSize > 0, "daily saying response buffer must be nonzero");
static_assert(kDailySayingResponseBufferSize >= kDailySayingLen,
              "daily saying response buffer must cover cached saying text");
static_assert(kDailySayingLen > kMaxSayingChars,
              "daily saying cache must exceed the accepted character limit plus terminator");
static_assert(kMaxSayingChars > 0, "daily saying character limit must be positive");
static_assert(kMaxSayingAttempts > 0, "daily saying retry count must be positive");
static_assert(kMaxSayingJsonDepth >= 0, "daily saying JSON search depth must be non-negative");
static_assert(kUtf8ContinuationMask == 0xC0, "UTF-8 continuation mask must cover two high bits");
static_assert(kUtf8ContinuationPrefix == 0x80, "UTF-8 continuation prefix must match 10xxxxxx bytes");
static_assert(kJsonObjectStart != kJsonArrayStart, "JSON object and array sentinels must differ");
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

class DailySayingJsonRoot {
public:
    explicit DailySayingJsonRoot(const char *response)
        : root_(cJSON_Parse(response))
    {
    }

    ~DailySayingJsonRoot()
    {
        cJSON_Delete(root_);
    }

    DailySayingJsonRoot(const DailySayingJsonRoot &) = delete;
    DailySayingJsonRoot &operator=(const DailySayingJsonRoot &) = delete;

    cJSON *get() const
    {
        return root_;
    }

    explicit operator bool() const
    {
        return root_ != nullptr;
    }

private:
    cJSON *root_;
};

bool copy_trimmed_saying_text(const char *text, char *out, size_t out_len)
{
    if (!text || !output_buffer_available(out, out_len)) {
        return false;
    }
    strlcpy(out, text, out_len);
    trim_ascii(out);
    return out[0] != '\0';
}

bool plain_text_saying_candidate(const char *text)
{
    return text && text[0] != '\0' && text[0] != kJsonObjectStart && text[0] != kJsonArrayStart;
}

bool copy_plain_text_saying_candidate(const char *response, char *out, size_t out_len)
{
    if (!response || !output_buffer_available(out, out_len)) {
        return false;
    }
    strlcpy(out, response, out_len);
    trim_ascii(out);
    return plain_text_saying_candidate(out);
}

bool daily_saying_json_depth_allowed(int depth)
{
    return depth <= kMaxSayingJsonDepth;
}

bool copy_json_saying_field(cJSON *obj, char *out, size_t out_len, int depth)
{
    if (!obj || !output_buffer_available(out, out_len)) {
        return false;
    }
    if (!daily_saying_json_depth_allowed(depth)) {
        return false;
    }
    if (cJSON_IsString(obj) && obj->valuestring) {
        return copy_trimmed_saying_text(obj->valuestring, out, out_len);
    }
    if (!cJSON_IsObject(obj)) {
        return false;
    }
    for (const char *field : kDailySayingJsonFields) {
        cJSON *item = cJSON_GetObjectItem(obj, field);
        if (!item) {
            continue;
        }
        if (cJSON_IsString(item) && item->valuestring) {
            return copy_trimmed_saying_text(item->valuestring, out, out_len);
        }
        if (cJSON_IsObject(item) && copy_json_saying_field(item, out, out_len, depth + 1)) {
            return true;
        }
    }
    return false;
}

bool extract_daily_saying(const char *response, char *out, size_t out_len)
{
    if (!response || !output_buffer_available(out, out_len)) {
        return false;
    }
    out[0] = '\0';
    DailySayingJsonRoot root(response);
    if (root) {
        bool ok = copy_json_saying_field(root.get(), out, out_len, 0);
        if (ok) {
            return true;
        }
    }
    return copy_plain_text_saying_candidate(response, out, out_len);
}

int utf8_char_count(const char *text)
{
    if (!text) {
        return 0;
    }
    int count = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if ((*p & kUtf8ContinuationMask) != kUtf8ContinuationPrefix) {
            ++count;
        }
        ++p;
    }
    return count;
}

bool saying_within_length(const char *text, int *chars_out)
{
    int chars = utf8_char_count(text);
    if (chars_out) {
        *chars_out = chars;
    }
    return chars <= kMaxSayingChars;
}

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
    bool ok = extract_daily_saying(response, out, out_len);
    if (!ok) {
        stats.record_parse_failure();
        ESP_LOGW(TAG, DAILY_SAYING_PARSE_FAILED_LOG_FORMAT);
        return false;
    }
    int chars = 0;
    if (saying_within_length(out, &chars)) {
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
