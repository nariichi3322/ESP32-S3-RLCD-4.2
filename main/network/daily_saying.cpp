// 获取、筛选和缓存图片时钟底部每日文字。
#include "network_services.h"

#include "app_constexpr.h"
#include "daily_saying_contract.h"
#include "daily_saying_state.h"
#include "daily_saying_parser.h"
#include "scoped_heap_buffer.h"
#include "ui_views.h"

#define DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT "daily saying response alloc failed"
#define DAILY_SAYING_HTTP_FAILED_LOG_FORMAT "daily saying http failed err=%s"
#define DAILY_SAYING_PARSE_FAILED_LOG_FORMAT "daily saying parse failed"
#define DAILY_SAYING_TOO_LONG_LOG_FORMAT "daily saying too long chars=%d attempt=%d"
#define DAILY_SAYING_UPDATE_FAILED_LOG_FORMAT "daily saying update failed attempts=%d http=%d parse=%d long=%d"
#define DAILY_SAYING_STATE_PUBLISH_FAILED_LOG_FORMAT "daily saying state publish failed"
#define DAILY_SAYING_UPDATED_LOG_FORMAT "daily saying updated"

namespace {
constexpr const char *kDailySayingUrl = "https://uapis.cn/api/v1/saying";
constexpr size_t kDailySayingResponseBufferSize = 768;
constexpr int kMaxSayingAttempts = 8;
constexpr uint32_t kDailySayingRetrySettleMs = 120;

static_assert(kDailySayingResponseBufferSize > 0, "daily saying response buffer must be nonzero");
static_assert(kDailySayingResponseBufferSize >= kDailySayingLen,
              "daily saying response buffer must cover cached saying text");
static_assert(kDailySayingLen > daily_saying_parser::kMaxChars,
              "daily saying cache must exceed the accepted character limit plus terminator");
static_assert(kMaxSayingAttempts > 0, "daily saying retry count must be positive");
static_assert(kDailySayingRetrySettleMs > 0,
              "daily saying retry settle delay must be positive");
static_assert(cstr_nonempty(kDailySayingUrl), "daily saying URL must be non-empty");

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

void settle_before_next_daily_saying_attempt(int attempt)
{
    if (attempt < kMaxSayingAttempts) {
        vTaskDelay(pdMS_TO_TICKS(kDailySayingRetrySettleMs));
    }
}
} // namespace

bool perform_daily_saying_update()
{
    if (battery_low_mode_load()) {
        return false;
    }
    char next[kDailySayingLen] = {};
    ScopedHeapBuffer<char> response(kDailySayingResponseBufferSize,
                                    HeapBufferInit::kZeroed);
    if (!response) {
        ESP_LOGW(TAG, DAILY_SAYING_RESPONSE_ALLOC_FAILED_LOG_FORMAT);
        return false;
    }
    DailySayingAttemptStats stats;
    for (int attempt = 1; attempt <= kMaxSayingAttempts; ++attempt) {
        if (setup_portal_start_requested()) {
            break;
        }
        response.clear();
        esp_err_t err = http_get_text(kDailySayingUrl, response.get(), response.size(), nullptr);
        if (setup_portal_start_requested()) {
            break;
        }
        if (err != ESP_OK) {
            stats.record_http_failure();
            ESP_LOGW(TAG, DAILY_SAYING_HTTP_FAILED_LOG_FORMAT, esp_err_to_name(err));
            settle_before_next_daily_saying_attempt(attempt);
            continue;
        }
        if (parse_daily_saying_attempt(response.get(), next, sizeof(next), attempt, stats)) {
            break;
        }
        settle_before_next_daily_saying_attempt(attempt);
    }
    if (next[0] == '\0') {
        log_daily_saying_update_failed(stats);
        return false;
    }
    time_t synced_at = 0;
    time(&synced_at);
    if (!daily_saying_state_publish(next, synced_at)) {
        ESP_LOGW(TAG, DAILY_SAYING_STATE_PUBLISH_FAILED_LOG_FORMAT);
        return false;
    }
    notify_ui_task();
    ESP_LOGI(TAG, DAILY_SAYING_UPDATED_LOG_FORMAT);
    return true;
}
