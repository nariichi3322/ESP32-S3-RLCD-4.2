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

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
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
    if (!text || !out || out_len == 0) {
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

bool daily_saying_json_depth_allowed(int depth)
{
    return depth <= kMaxSayingJsonDepth;
}

bool copy_json_saying_field(cJSON *obj, char *out, size_t out_len, int depth)
{
    if (!obj || !out || out_len == 0) {
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
    if (!response || !out || out_len == 0) {
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
    strlcpy(out, response, out_len);
    trim_ascii(out);
    return plain_text_saying_candidate(out);
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
} // namespace

void load_daily_saying_cache()
{
    g_daily_saying[0] = '\0';
    g_last_saying_sync_time = 0;
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
    int http_failures = 0;
    int parse_failures = 0;
    int long_responses = 0;
    for (int attempt = 1; attempt <= kMaxSayingAttempts; ++attempt) {
        response.clear();
        esp_err_t err = http_get_text(kDailySayingUrl, response.get(), response.size(), nullptr);
        if (err != ESP_OK) {
            ++http_failures;
            ESP_LOGW(TAG, DAILY_SAYING_HTTP_FAILED_LOG_FORMAT, esp_err_to_name(err));
            continue;
        }
        bool ok = extract_daily_saying(response.get(), next, sizeof(next));
        if (!ok) {
            ++parse_failures;
            ESP_LOGW(TAG, DAILY_SAYING_PARSE_FAILED_LOG_FORMAT);
            continue;
        }
        int chars = 0;
        if (saying_within_length(next, &chars)) {
            break;
        }
        ++long_responses;
        ESP_LOGW(TAG, DAILY_SAYING_TOO_LONG_LOG_FORMAT, chars, attempt);
        next[0] = '\0';
    }
    if (next[0] == '\0') {
        ESP_LOGW(TAG,
                 DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT,
                 kMaxSayingAttempts,
                 http_failures,
                 parse_failures,
                 long_responses);
        return false;
    }
    strlcpy(g_daily_saying, next, sizeof(g_daily_saying));
    time(&g_last_saying_sync_time);
    notify_ui_task();
    ESP_LOGI(TAG, DAILY_SAYING_UPDATED_LOG_FORMAT);
    return true;
}
