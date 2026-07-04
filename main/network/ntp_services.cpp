// 执行 NTP 时间同步并维护系统可信时间状态。
#include "network_services.h"

#include "sensor_services.h"

#include "sdkconfig.h"

#define NTP_SYNCED_LOG_FORMAT "ntp synced: %04d-%02d-%02d %02d:%02d:%02d"
#define NTP_TIMEOUT_LOG_FORMAT "ntp sync timeout retries=%d poll_ms=%lu"
#define NTP_INVALID_RETRY_COUNT_LOG_FORMAT "ntp sync invalid retry count: %d"

namespace {
constexpr const char *const kNtpServers[] = {
    "pool.ntp.org",
    "ntp.aliyun.com",
    "time.windows.com",
};
template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr size_t kNtpServerCount = array_count(kNtpServers);
constexpr size_t kDefaultConfiguredNtpServerSlots = 1;
#ifdef CONFIG_LWIP_SNTP_MAX_SERVERS
constexpr size_t kConfiguredNtpServerSlots = CONFIG_LWIP_SNTP_MAX_SERVERS;
#else
constexpr size_t kConfiguredNtpServerSlots = kDefaultConfiguredNtpServerSlots;
#endif
constexpr uint32_t kNtpPollDelayMs = 1000;
constexpr int kTmYearOffset = 1900;
constexpr int kTmMonthOffset = 1;
static_assert(kNtpServerCount > 0, "at least one NTP server is required");
static_assert(kConfiguredNtpServerSlots > 0, "SNTP must support at least one configured server");

constexpr size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *text : items) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

constexpr size_t kActiveNtpServerCount = min_size(kNtpServerCount, kConfiguredNtpServerSlots);
constexpr TickType_t kNtpPollDelay = pdMS_TO_TICKS(kNtpPollDelayMs);
constexpr const char *kNtpTimeSyncedEventUnavailableLog = "skip time synced event bit: app events unavailable";
constexpr const char *const kNtpLogTexts[] = {
    NTP_SYNCED_LOG_FORMAT,
    NTP_TIMEOUT_LOG_FORMAT,
    NTP_INVALID_RETRY_COUNT_LOG_FORMAT,
    kNtpTimeSyncedEventUnavailableLog,
};

static_assert(cstr_array_nonempty(kNtpServers), "NTP server names must be non-empty");
static_assert(array_count(kNtpLogTexts) > 0,
              "NTP log guard must cover log text");
static_assert(cstr_array_nonempty(kNtpLogTexts), "NTP log texts must be non-empty");
static_assert(kDefaultConfiguredNtpServerSlots > 0, "default SNTP server slots must be positive");
static_assert(kActiveNtpServerCount > 0, "active NTP server count must be positive");
static_assert(kActiveNtpServerCount <= kNtpServerCount, "active NTP server count must fit server list");
static_assert(kActiveNtpServerCount <= kConfiguredNtpServerSlots, "active NTP server count must fit SNTP slots");
static_assert(kNtpPollDelayMs > 0, "NTP poll delay must be positive");
static_assert(kNtpPollDelay > 0, "NTP poll tick delay must be positive");
static_assert(kTmYearOffset == 1900, "struct tm year offset must stay 1900");
static_assert(kTmMonthOffset == 1, "struct tm month offset must stay 1");

void set_time_synced_event_bit()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kNtpTimeSyncedEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kTimeSyncedBit);
}

void configure_ntp_servers()
{
    for (size_t i = 0; i < kActiveNtpServerCount; ++i) {
        esp_sntp_setservername(i, kNtpServers[i]);
    }
}

void start_or_restart_ntp()
{
    if (!g_ntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        configure_ntp_servers();
        esp_sntp_init();
        g_ntp_started = true;
        return;
    }
    esp_sntp_restart();
}

void log_ntp_synced_time(const struct tm &local)
{
    ESP_LOGI(TAG, NTP_SYNCED_LOG_FORMAT,
             local.tm_year + kTmYearOffset, local.tm_mon + kTmMonthOffset, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec);
}

bool ntp_synced_time_available(struct tm *local)
{
    return local &&
           esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
           is_system_time_plausible(local);
}

bool wait_for_ntp_synced_time(int max_retries, struct tm *synced_time)
{
    if (!synced_time) {
        return false;
    }
    for (int retry = 0; retry < max_retries; ++retry) {
        struct tm local = {};
        if (ntp_synced_time_available(&local)) {
            *synced_time = local;
            return true;
        }
        vTaskDelay(kNtpPollDelay);
    }
    return false;
}
} // namespace

bool perform_ntp_sync(int max_retries)
{
    if (max_retries <= 0) {
        ESP_LOGW(TAG, NTP_INVALID_RETRY_COUNT_LOG_FORMAT, max_retries);
        return false;
    }
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    start_or_restart_ntp();

    struct tm local = {};
    if (wait_for_ntp_synced_time(max_retries, &local)) {
        sync_rtc_from_system_time();
        time(&g_last_ntp_sync_time);
        set_time_synced_event_bit();
        log_ntp_synced_time(local);
        return true;
    }
    ESP_LOGW(TAG, NTP_TIMEOUT_LOG_FORMAT,
             max_retries,
             (unsigned long)kNtpPollDelayMs);
    return false;
}
