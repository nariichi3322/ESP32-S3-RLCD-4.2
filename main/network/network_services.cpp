// 调度 NTP、天气、预警、每日文字和 OTA 等联网同步流程。
#include "network_services.h"

#include "ota_runtime_state.h"

#include "network_https_resources.h"
#include "network_diagnostics_catalog.h"
#include "network_sync_schedule.h"
#include "network_task_guards.h"
#include "sensor_services.h"
#include "startup_state.h"
#include "ui_views.h"
#include "wifi_portal_state.h"
#include "wifi_radio_state.h"

static constexpr uint32_t kNetworkOtaActiveWaitMs = 10000;
static constexpr time_t kSecondsPerMinute = 60;
static constexpr time_t kMinutesPerHour = 60;
static constexpr time_t kHoursPerDay = 24;
static constexpr time_t kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
static constexpr time_t kSecondsPerDay = kHoursPerDay * kSecondsPerHour;
static constexpr uint32_t kNetworkNoWorkWaitMs = 30000;
static constexpr uint32_t kNetworkShortRetryWaitMs = 1000;
static constexpr uint32_t kNetworkWifiConnectTimeoutMs = 45000;
static constexpr uint32_t kNetworkTaskStartupDelayMs = 2500;
static constexpr uint32_t kNetworkBootSyncGateWarningMs = 1000;
static constexpr time_t kNetworkNtpRetryDelaySec = 5 * kSecondsPerMinute;
static constexpr time_t kBootWeatherRefreshDelaySec = 10;
static constexpr time_t kBootSayingRefreshDelaySec = 25;
static constexpr time_t kBootHttpsInterRequestGapSec = 8;
static constexpr uint32_t kBootHttpsMemoryRetryMs = 10000;
static_assert(kBootWeatherRefreshDelaySec > 0,
              "boot weather refresh delay must be positive");
static_assert(kBootSayingRefreshDelaySec > kBootWeatherRefreshDelaySec,
              "boot saying refresh delay must stay after boot weather refresh");
static_assert(kBootHttpsInterRequestGapSec > 0,
              "boot HTTPS inter-request gap must be positive");
static_assert(kBootHttpsMemoryRetryMs >= 1000,
              "boot HTTPS memory retry must avoid a tight loop");
static_assert(kNetworkBootSyncGateWarningMs > 0,
              "boot sync gate warning delay must be positive");
static_assert(kNetworkDiagOtaLine == kNetworkDiagLineCount - 1,
              "network service diagnostics line mapping must match diagnostics line count");
static constexpr const char *kNetworkStatusOfflineModeEnabled = "离线模式已开启";
static constexpr const char *kNetworkStatusWifiNotConfigured = "未配置 WiFi";
static constexpr const char *kNetworkDiagLocalIpPlaceholder = "本地IP: --";
static constexpr const char *kNetworkDiagPublicIpPlaceholder = "公网IP: --";
static constexpr const char *kNetworkDiagIpLocationWifiNotConfigured = "IP定位: WiFi未配置";
static constexpr const char *kNetworkDiagIpLocationWifiStartFailed = "IP定位: WiFi启动失败";
static constexpr const char *kNetworkDiagIpLocationWifiConnectTimeout = "IP定位: WiFi连接超时";
static constexpr const char *kNetworkDiagDnsUnchecked = "DNS: 未检测";
static constexpr const char *kNetworkDiagWeatherUnchecked = "天气: 未检测";
static constexpr const char *kNetworkDiagNtpUnchecked = "NTP: 未检测";
static constexpr const char *kNetworkDiagSayingUnchecked = "一言: 未检测";
static constexpr const char *kNetworkDiagInternetUnchecked = "公网: 未检测";
static constexpr const char *kNetworkDiagOtaSourceUnchecked = "OTA源: 未检测";
static constexpr const char *kNetworkSyncTimeComplete = "时间同步完成";
static constexpr const char *kNetworkSyncWeatherComplete = "天气同步完成";
static constexpr const char *kNetworkSyncSayingComplete = "一言更新完成";
static constexpr const char *kNetworkSyncNetworkDiagComplete = "网络检测完成";
static constexpr const char *kNetworkSyncTimeFailed = "时间同步失败";
static constexpr const char *kNetworkSyncWeatherFailed = "天气同步失败";
static constexpr const char *kNetworkSyncSayingFailed = "一言更新失败";
static constexpr const char *kNetworkSyncWeatherKeyMissing = "未配置 API Key";
static constexpr const char *kNetworkSyncLowBatterySkipped = "电量低，已跳过";
static constexpr const char *kNetworkWifiWaitSkippedLog = "wifi wait skipped: app events unavailable";
static constexpr const char *kNetworkCacheTimeConversionSkippedLog = "cache time conversion skipped: output is null";
static constexpr const char *kNetworkCacheUnknownLabel = "unknown";
#define NETWORK_CACHE_TIME_CONVERSION_FAILED_FORMAT "%s cache time conversion failed"
#define NETWORK_BOOT_REFRESH_SCHEDULED_FORMAT "boot network refresh scheduled: weather=%d saying=%d"
#define NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT \
    "background boot HTTPS deferred: internal_free=%u internal_largest=%u dma_largest=%u"
#define NETWORK_BOOT_SAYING_STAGGERED_FORMAT \
    "boot daily saying deferred %lld seconds after weather"
#define NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT \
    "boot weather resource retry deferred %lld seconds"
static constexpr const char *kNetworkDiagWifiOnLog = "wifi radio on for network diagnostics";
#define NETWORK_SYNC_WIFI_ON_FORMAT "wifi radio on for sync: ntp=%d weather=%d saying=%d boot_weather=%d boot_saying=%d"
static constexpr const char *kNetworkSyncWifiStartFailedLog = "wifi start failed during sync window";
static constexpr const char *kNetworkSyncWifiConnectTimeoutLog = "wifi connect timeout during sync window";
static constexpr const char *kNetworkBootSyncGateWaitLog = "network sync waiting for boot connectivity task";

bool wait_for_wifi_connected(uint32_t timeout_ms)
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kNetworkWifiWaitSkippedLog);
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(
        g_app_events,
        kWifiConnectedBit,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & kWifiConnectedBit) != 0;
}

bool is_time_valid(struct tm *local_out)
{
    return is_system_time_plausible(local_out);
}

bool enabled_weather_data_page_exists()
{
    return is_work_page_enabled(kWorkPageWeatherClock) ||
           is_work_page_enabled(kWorkPageWeatherBoard);
}

bool enabled_daily_saying_page_exists()
{
    return is_work_page_enabled(kWorkPageGallery);
}

void wait_for_network_sync_event(uint32_t timeout_ms)
{
    xEventGroupWaitBits(g_app_events,
                        kProvisioningSyncBit | kManualNtpSyncBit | kManualWeatherSyncBit | kManualSayingSyncBit | kNetworkDiagBit,
                        pdFALSE,
                        pdFALSE,
                        pdMS_TO_TICKS(timeout_ms));
}

void schedule_ntp_retry(time_t *next_ntp_retry_at)
{
    if (!next_ntp_retry_at) {
        return;
    }
    time(next_ntp_retry_at);
    *next_ntp_retry_at += kNetworkNtpRetryDelaySec;
}

void set_network_diag_unavailable(const char *ip_location_text)
{
    network_diag_set_line(kNetworkDiagLocalIpLine, kNetworkDiagLocalIpPlaceholder);
    network_diag_set_line(kNetworkDiagPublicIpLine, kNetworkDiagPublicIpPlaceholder);
    network_diag_set_line(kNetworkDiagIpLocationLine, ip_location_text);
    network_diag_set_line(kNetworkDiagDnsLine, kNetworkDiagDnsUnchecked);
    network_diag_set_line(kNetworkDiagWeatherLine, kNetworkDiagWeatherUnchecked);
    network_diag_set_line(kNetworkDiagNtpLine, kNetworkDiagNtpUnchecked);
    network_diag_set_line(kNetworkDiagSayingLine, kNetworkDiagSayingUnchecked);
    network_diag_set_line(kNetworkDiagInternetLine, kNetworkDiagInternetUnchecked);
    network_diag_set_line(kNetworkDiagOtaLine, kNetworkDiagOtaSourceUnchecked);
}

static bool localtime_for_cache_check(time_t value, struct tm *out, const char *label)
{
    if (!out) {
        ESP_LOGW(TAG, "%s", kNetworkCacheTimeConversionSkippedLog);
        return false;
    }
    if (!localtime_r(&value, out)) {
        ESP_LOGW(TAG, NETWORK_CACHE_TIME_CONVERSION_FAILED_FORMAT, label ? label : kNetworkCacheUnknownLabel);
        return false;
    }
    return true;
}

static bool weather_cache_current_hour(time_t now)
{
    const time_t last_sync_time = get_last_weather_sync_time();
    if (last_sync_time <= 0) {
        return false;
    }
    struct tm now_local = {};
    struct tm last_local = {};
    if (!localtime_for_cache_check(now, &now_local, "weather now") ||
        !localtime_for_cache_check(last_sync_time, &last_local, "weather last") ||
        !is_tm_plausible(now_local) ||
        !is_tm_plausible(last_local)) {
        return network_cache_age_is_fresh(now, last_sync_time, kSecondsPerHour);
    }
    return network_cache_local_hour_matches(now_local, last_local);
}

static bool saying_cache_current_day(time_t now)
{
    char saying[kDailySayingLen] = {};
    time_t last_sync_time = 0;
    if (!get_daily_saying_snapshot(saying, sizeof(saying), &last_sync_time) ||
        last_sync_time <= 0) {
        return false;
    }
    struct tm now_local = {};
    struct tm last_local = {};
    if (!localtime_for_cache_check(now, &now_local, "saying now") ||
        !localtime_for_cache_check(last_sync_time, &last_local, "saying last") ||
        !is_tm_plausible(now_local) ||
        !is_tm_plausible(last_local)) {
        return network_cache_age_is_fresh(now, last_sync_time, kSecondsPerDay);
    }
    return network_cache_local_day_matches(now_local, last_local);
}

static void clear_ready_boot_sync_flags(bool weather_ready, bool saying_ready, bool *weather_due, bool *saying_due)
{
    if (weather_ready && weather_due) {
        *weather_due = false;
    }
    if (saying_ready && saying_due) {
        *saying_due = false;
    }
}

static void finish_requested_settings_sync(bool requested,
                                           SettingsSyncOp op,
                                           const char *status,
                                           EventBits_t bit,
                                           bool clear_bit)
{
    if (!requested) {
        return;
    }
    finish_settings_sync(op, status);
    if (clear_bit) {
        xEventGroupClearBits(g_app_events, bit);
    }
}

static void finish_settings_sync_and_clear_bit(SettingsSyncOp op, const char *status, EventBits_t bit)
{
    finish_requested_settings_sync(true, op, status, bit, true);
}

struct NetworkSyncRequestSnapshot {
    bool provisioning = false;
    bool manual_ntp = false;
    bool manual_weather = false;
    bool manual_saying = false;
    bool diagnostics = false;

    bool none_for_setup_portal() const
    {
        return !provisioning && !manual_ntp && !manual_weather &&
               !manual_saying && !diagnostics;
    }
};

static void finish_requested_manual_syncs(const NetworkSyncRequestSnapshot &requests,
                                          const char *status,
                                          bool clear_bits)
{
    finish_requested_settings_sync(requests.manual_ntp,
                                   kSettingsSyncNtp,
                                   status,
                                   kManualNtpSyncBit,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_weather,
                                   kSettingsSyncWeather,
                                   status,
                                   kManualWeatherSyncBit,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_saying,
                                   kSettingsSyncSaying,
                                   status,
                                   kManualSayingSyncBit,
                                   clear_bits);
}

static NetworkSyncRequestSnapshot snapshot_network_sync_requests()
{
    // A loop must schedule and finish the same event snapshot even if another
    // task raises a new request while HTTPS is in progress.
    EventBits_t bits = xEventGroupGetBits(g_app_events);
    NetworkSyncRequestSnapshot requests;
    requests.provisioning = (bits & kProvisioningSyncBit) != 0;
    requests.manual_ntp = (bits & kManualNtpSyncBit) != 0;
    requests.manual_weather = (bits & kManualWeatherSyncBit) != 0;
    requests.manual_saying = (bits & kManualSayingSyncBit) != 0;
    requests.diagnostics = (bits & kNetworkDiagBit) != 0;
    return requests;
}

static void finish_offline_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (wifi_radio_on_load() && !setup_portal_active_load()) {
        stop_wifi_radio(true);
    }
    finish_requested_manual_syncs(requests, kNetworkStatusOfflineModeEnabled, false);
    if (requests.diagnostics) {
        network_diag_begin();
        for (int i = 0; i < kNetworkDiagLineCount; ++i) {
            network_diag_set_line(i, kNetworkStatusOfflineModeEnabled);
        }
        network_diag_finish();
        finish_settings_sync(kSettingsSyncNetworkDiag, kNetworkStatusOfflineModeEnabled);
    }
    clear_network_request_bits();
}

static void finish_unconfigured_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    finish_requested_manual_syncs(requests, kNetworkStatusWifiNotConfigured, true);
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.diagnostics) {
        network_diag_begin();
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiNotConfigured);
        network_diag_finish();
        finish_settings_sync_and_clear_bit(kSettingsSyncNetworkDiag,
                                           kNetworkSyncNetworkDiagComplete,
                                           kNetworkDiagBit);
    }
}

static void finish_network_radio_session(NetworkAwakeLockGuard &awake_lock,
                                         bool force_setup_portal = false)
{
    // Keep the CPU awake through esp_wifi_stop(), then service any deferred
    // close request after this session releases its final PM-lock ownership.
    stop_wifi_radio(force_setup_portal);
    awake_lock.release();
    service_wifi_radio_stop_when_idle();
}

static void settle_between_network_operations(bool more_work_pending)
{
    if (more_work_pending) {
        // The preceding client has already released its TLS buffers. Give the
        // allocator and UI task a scheduling window before the next operation.
        // The first minute uses a longer gap because boot UI, sensor and cache
        // initialization still compete for internal/DMA memory.
        bool startup_pressure = network_startup_pressure_window_active(
            startup_screen_active(),
            esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(
            network_inter_operation_settle_delay_ms(startup_pressure)));
    }
}

static bool background_boot_https_memory_ready()
{
    const NetworkHttpsMemorySnapshot memory = capture_network_https_memory_snapshot();
    bool allowed = network_automatic_boot_https_allowed(startup_screen_active(),
                                                        esp_timer_get_time(),
                                                        memory.internal_free,
                                                        memory.internal_largest,
                                                        memory.dma_largest);
    if (!allowed) {
        ESP_LOGW(TAG,
                 NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT,
                 static_cast<unsigned>(memory.internal_free),
                 static_cast<unsigned>(memory.internal_largest),
                 static_cast<unsigned>(memory.dma_largest));
    }
    return allowed;
}

static bool defer_automatic_boot_https_for_memory(NetworkSyncSchedule *schedule,
                                                  const NetworkSyncRequestSnapshot &requests,
                                                  time_t now,
                                                  time_t *boot_weather_due_at,
                                                  time_t *boot_saying_due_at)
{
    if (!schedule) {
        return false;
    }
    bool auto_weather = schedule->boot_weather_ready &&
                        !requests.provisioning && !requests.manual_weather;
    bool auto_saying = schedule->boot_saying_ready &&
                       !requests.provisioning && !requests.manual_saying;
    if ((!auto_weather && !auto_saying) || background_boot_https_memory_ready()) {
        return false;
    }
    time_t retry_at = now + static_cast<time_t>(kBootHttpsMemoryRetryMs / 1000);
    if (auto_weather) {
        schedule->weather_due = false;
        schedule->boot_weather_ready = false;
        schedule->stagger_boot_saying_after_weather = false;
        if (boot_weather_due_at) {
            *boot_weather_due_at = retry_at;
        }
    }
    if (auto_saying) {
        schedule->saying_due = false;
        schedule->boot_saying_ready = false;
        if (boot_saying_due_at) {
            *boot_saying_due_at = retry_at;
        }
    }
    return true;
}

static void stagger_boot_saying_after_weather(const NetworkSyncSchedule &schedule,
                                              bool boot_saying_due,
                                              time_t *boot_saying_due_at)
{
    if (!schedule.stagger_boot_saying_after_weather ||
        !boot_saying_due || !boot_saying_due_at) {
        return;
    }
    time_t now = 0;
    time(&now);
    *boot_saying_due_at = now + kBootHttpsInterRequestGapSec;
    ESP_LOGI(TAG,
             NETWORK_BOOT_SAYING_STAGGERED_FORMAT,
             static_cast<long long>(kBootHttpsInterRequestGapSec));
}

static void finish_failed_sync_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           kNetworkSyncTimeFailed,
                                           kManualNtpSyncBit);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           kNetworkSyncWeatherFailed,
                                           kManualWeatherSyncBit);
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           kNetworkSyncSayingFailed,
                                           kManualSayingSyncBit);
    }
}

static void finish_successful_sync_requests(const NetworkSyncRequestSnapshot &requests,
                                            bool ntp_ok,
                                            bool weather_ok,
                                            bool saying_ok)
{
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           ntp_ok ? kNetworkSyncTimeComplete : kNetworkSyncTimeFailed,
                                           kManualNtpSyncBit);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           weather_ok ? kNetworkSyncWeatherComplete : kNetworkSyncWeatherFailed,
                                           kManualWeatherSyncBit);
        notify_ui_task();
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           saying_ok ? kNetworkSyncSayingComplete : kNetworkSyncSayingFailed,
                                           kManualSayingSyncBit);
        notify_ui_task();
    }
}

static void finalize_failed_network_sync_window(const NetworkSyncSchedule &schedule,
                                                const NetworkSyncRequestSnapshot &requests,
                                                bool *boot_weather_due,
                                                bool *boot_saying_due,
                                                time_t *next_ntp_retry_at)
{
    clear_ready_boot_sync_flags(schedule.boot_weather_ready,
                                schedule.boot_saying_ready,
                                boot_weather_due,
                                boot_saying_due);
    finish_failed_sync_requests(requests);
    if (schedule.ntp_due) {
        schedule_ntp_retry(next_ntp_retry_at);
    }
}

static void execute_connected_sync_window(const NetworkSyncSchedule &schedule,
                                          const NetworkSyncRequestSnapshot &requests,
                                          bool &boot_ntp_due,
                                          bool &boot_weather_due,
                                          bool &boot_saying_due,
                                          time_t &boot_weather_due_at,
                                          time_t &next_ntp_retry_at,
                                          int &last_midnight_ntp_yday)
{
    bool ntp_ok = false;
    bool weather_ok = false;
    bool weather_resource_deferred = false;
    bool saying_ok = false;
    NetworkDisplayDmaGuard display_guard(schedule.weather_due || schedule.saying_due);
    if (schedule.ntp_due) {
        if (perform_ntp_sync()) {
            ntp_ok = true;
            boot_ntp_due = false;
            next_ntp_retry_at = 0;
            struct tm synced_local = {};
            if (is_time_valid(&synced_local) && synced_local.tm_hour == 0) {
                last_midnight_ntp_yday = synced_local.tm_yday;
            }
        } else {
            schedule_ntp_retry(&next_ntp_retry_at);
        }
        settle_between_network_operations(schedule.weather_due ||
                                          schedule.saying_due);
    }
    if (schedule.weather_due) {
        WeatherUpdateResult result = perform_weather_update();
        weather_ok = result == WeatherUpdateResult::kSuccess;
        weather_resource_deferred = result == WeatherUpdateResult::kResourceDeferred;
        settle_between_network_operations(schedule.saying_due);
    }
    if (schedule.saying_due) {
        saying_ok = perform_daily_saying_update();
    }
    boot_weather_due = network_boot_weather_due_after_update(
        boot_weather_due,
        schedule.boot_weather_ready,
        weather_resource_deferred);
    if (schedule.boot_weather_ready && weather_resource_deferred) {
        time_t now = 0;
        time(&now);
        boot_weather_due_at = now + static_cast<time_t>(kBootHttpsMemoryRetryMs / 1000);
        ESP_LOGI(TAG,
                 NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT,
                 static_cast<long long>(kBootHttpsMemoryRetryMs / 1000));
    }
    clear_ready_boot_sync_flags(false,
                                schedule.boot_saying_ready,
                                nullptr,
                                &boot_saying_due);
    finish_successful_sync_requests(requests,
                                    ntp_ok,
                                    weather_ok,
                                    saying_ok);
}

static void execute_network_diagnostics_window()
{
    ESP_LOGI(TAG, "%s", kNetworkDiagWifiOnLog);
    NetworkAwakeLockGuard awake_lock;
    network_diag_begin();
    if (!start_wifi_radio(false)) {
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiStartFailed);
    } else if (!wait_for_wifi_connected(kNetworkWifiConnectTimeoutMs)) {
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiConnectTimeout);
    } else {
        run_network_diagnostic_checks();
    }
    finish_network_radio_session(awake_lock);
    network_diag_finish();
    finish_settings_sync_and_clear_bit(kSettingsSyncNetworkDiag,
                                       kNetworkSyncNetworkDiagComplete,
                                       kNetworkDiagBit);
}

static void wait_for_boot_sync_completion()
{
    EventBits_t bits = xEventGroupWaitBits(g_app_events,
                                          kBootSyncDoneBit,
                                          pdFALSE,
                                          pdTRUE,
                                          pdMS_TO_TICKS(kNetworkBootSyncGateWarningMs));
    if ((bits & kBootSyncDoneBit) != 0) {
        return;
    }
    ESP_LOGW(TAG, "%s", kNetworkBootSyncGateWaitLog);
    xEventGroupWaitBits(g_app_events,
                        kBootSyncDoneBit,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY);
}

void network_sync_task(void *)
{
    wait_for_boot_sync_completion();
    vTaskDelay(pdMS_TO_TICKS(kNetworkTaskStartupDelayMs));
    EventBits_t initial_bits = xEventGroupGetBits(g_app_events);
    bool boot_ntp_due = (initial_bits & kTimeSyncedBit) == 0;
    time_t next_ntp_retry_at = 0;
    int last_midnight_ntp_yday = -1;
    time_t boot_schedule_now = 0;
    time(&boot_schedule_now);
    bool boot_weather_due = g_have_wifi_creds &&
                            g_have_weather_key &&
                            !g_offline_mode_ui_enabled &&
                            !battery_low_mode_load() &&
                            enabled_weather_data_page_exists();
    bool boot_saying_due = g_have_wifi_creds &&
                           !g_offline_mode_ui_enabled &&
                           !battery_low_mode_load() &&
                           enabled_daily_saying_page_exists();
    time_t boot_weather_due_at = boot_schedule_now + kBootWeatherRefreshDelaySec;
    time_t boot_saying_due_at = boot_schedule_now + kBootSayingRefreshDelaySec;
    if (boot_weather_due || boot_saying_due) {
        ESP_LOGI(TAG, NETWORK_BOOT_REFRESH_SCHEDULED_FORMAT, boot_weather_due, boot_saying_due);
    }

    for (;;) {
        NetworkSyncRequestSnapshot requests = snapshot_network_sync_requests();
        int ota_state = ota_runtime_state_load();
        if (ota_state == kOtaChecking || ota_state == kOtaUpdating) {
            // Keep queued requests intact, but do not let their level-triggered
            // event bits turn the OTA protection branch into a busy loop.
            vTaskDelay(pdMS_TO_TICKS(kNetworkOtaActiveWaitMs));
            continue;
        }
        if (g_offline_mode_ui_enabled) {
            boot_weather_due = false;
            boot_saying_due = false;
            finish_offline_network_requests(requests);
            wait_for_network_sync_event(kNetworkNoWorkWaitMs);
            continue;
        }
        if (!g_have_wifi_creds) {
            boot_weather_due = false;
            boot_saying_due = false;
            finish_unconfigured_network_requests(requests);
            wait_for_network_sync_event(kNetworkNoWorkWaitMs);
            continue;
        }
        if (requests.manual_weather && !g_have_weather_key) {
            finish_settings_sync_and_clear_bit(kSettingsSyncWeather, kNetworkSyncWeatherKeyMissing, kManualWeatherSyncBit);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }
        if (setup_portal_active_load() && requests.none_for_setup_portal()) {
            wait_for_network_sync_event(kNetworkNoWorkWaitMs);
            continue;
        }

        if (requests.diagnostics) {
            execute_network_diagnostics_window();
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }

        struct tm local = {};
        bool time_valid = is_time_valid(&local);
        bool midnight_ntp_due = time_valid &&
                                local.tm_hour == 0 &&
                                local.tm_min == 0 &&
                                local.tm_yday != last_midnight_ntp_yday;
        time_t now;
        time(&now);
        // A short boot request may obtain current conditions while forecast or
        // air quality times out. Keep the staggered full refresh scheduled in
        // that partial state so the weather board is ready before first entry.
        if (boot_weather_due &&
            weather_cache_current_hour(now) &&
            weather_extended_data_ready()) {
            boot_weather_due = false;
        }
        if (boot_saying_due && saying_cache_current_day(now)) {
            boot_saying_due = false;
        }
        if (battery_low_mode_load()) {
            boot_weather_due = false;
            boot_saying_due = false;
        }
        NetworkSyncScheduleInput schedule_input = {};
        schedule_input.now = now;
        schedule_input.next_ntp_retry_at = next_ntp_retry_at;
        schedule_input.boot_weather_due_at = boot_weather_due_at;
        schedule_input.boot_saying_due_at = boot_saying_due_at;
        schedule_input.have_weather_key = g_have_weather_key;
        schedule_input.low_battery_mode = battery_low_mode_load();
        schedule_input.provisioning_sync_due = requests.provisioning;
        schedule_input.manual_ntp_due = requests.manual_ntp;
        schedule_input.manual_weather_due = requests.manual_weather;
        schedule_input.manual_saying_due = requests.manual_saying;
        schedule_input.boot_ntp_due = boot_ntp_due;
        schedule_input.midnight_ntp_due = midnight_ntp_due;
        schedule_input.boot_weather_due = boot_weather_due;
        schedule_input.boot_saying_due = boot_saying_due;
        NetworkSyncSchedule schedule = calculate_network_sync_schedule(schedule_input);
        bool boot_https_memory_deferred = defer_automatic_boot_https_for_memory(
            &schedule,
            requests,
            now,
            &boot_weather_due_at,
            &boot_saying_due_at);
        if (battery_low_mode_load() && requests.manual_weather) {
            finish_settings_sync_and_clear_bit(kSettingsSyncWeather, kNetworkSyncLowBatterySkipped, kManualWeatherSyncBit);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }
        if (battery_low_mode_load() && requests.manual_saying) {
            finish_settings_sync_and_clear_bit(kSettingsSyncSaying, kNetworkSyncLowBatterySkipped, kManualSayingSyncBit);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }

        if (!schedule.ntp_due && !schedule.weather_due && !schedule.saying_due) {
            uint32_t wait_ms = boot_https_memory_deferred
                                   ? kBootHttpsMemoryRetryMs
                                   : network_idle_wait_ms(now,
                                                          schedule.next_boot_due_at,
                                                          next_ntp_retry_at);
            wait_for_network_sync_event(wait_ms);
            continue;
        }

        ESP_LOGI(TAG,
                 NETWORK_SYNC_WIFI_ON_FORMAT,
                 schedule.ntp_due,
                 schedule.weather_due,
                 schedule.saying_due,
                 schedule.boot_weather_ready,
                 schedule.boot_saying_ready);
        NetworkAwakeLockGuard awake_lock;
        if (!start_wifi_radio(false)) {
            ESP_LOGW(TAG, "%s", kNetworkSyncWifiStartFailedLog);
            finalize_failed_network_sync_window(schedule,
                                                requests,
                                                &boot_weather_due,
                                                &boot_saying_due,
                                                &next_ntp_retry_at);
            stagger_boot_saying_after_weather(schedule,
                                              boot_saying_due,
                                              &boot_saying_due_at);
            finish_network_radio_session(awake_lock, requests.provisioning);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }
        if (wait_for_wifi_connected(kNetworkWifiConnectTimeoutMs)) {
            execute_connected_sync_window(schedule,
                                          requests,
                                          boot_ntp_due,
                                          boot_weather_due,
                                          boot_saying_due,
                                          boot_weather_due_at,
                                          next_ntp_retry_at,
                                          last_midnight_ntp_yday);
            stagger_boot_saying_after_weather(schedule,
                                              boot_saying_due,
                                              &boot_saying_due_at);
        } else {
            ESP_LOGW(TAG, "%s", kNetworkSyncWifiConnectTimeoutLog);
            finalize_failed_network_sync_window(schedule,
                                                requests,
                                                &boot_weather_due,
                                                &boot_saying_due,
                                                &next_ntp_retry_at);
            stagger_boot_saying_after_weather(schedule,
                                              boot_saying_due,
                                              &boot_saying_due_at);
        }
        finish_network_radio_session(awake_lock, requests.provisioning);
    }
}
