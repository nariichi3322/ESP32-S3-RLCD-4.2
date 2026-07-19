// 调度 NTP、天气、预警、每日文字和 OTA 等联网同步流程。
#include "network_sync_task.h"

#include "app_event_group.h"
#include "daily_saying_contract.h"
#include "daily_saying_state.h"
#include "ota_runtime_state.h"

#include "network_https_resources.h"
#include "network_cache_policy.h"
#include "network_credentials_state.h"
#include "network_diagnostics.h"
#include "network_diagnostics_state.h"
#include "ntp_services.h"
#include "offline_mode_state.h"
#include "provisioning_validation.h"
#include "network_diagnostics_catalog.h"
#include "network_sync_requests.h"
#include "network_sync_schedule.h"
#include "network_task_guards.h"
#include "sensor_time.h"
#include "setup_portal_control.h"
#include "startup_state.h"
#include "ui_info_page_state.h"
#include "ui_settings_activity_state.h"
#include "ui_views.h"
#include "weather_state.h"
#include "weather_update.h"
#include "wifi_portal_state.h"
#include "wifi_radio_services.h"
#include "wifi_radio_state.h"

static constexpr uint32_t kNetworkNoWorkWaitMs = 30000;
static constexpr uint32_t kNetworkShortRetryWaitMs = 1000;
static constexpr uint32_t kNetworkWifiConnectTimeoutMs = 45000;
static constexpr uint32_t kNetworkTaskStartupDelayMs = 2500;
static constexpr uint32_t kNetworkBootSyncGateWarningMs = 1000;
static constexpr EventBits_t kNetworkSyncWakeBits = kProvisioningSyncBit |
                                                     kManualNtpSyncBit |
                                                     kManualWeatherSyncBit |
                                                     kManualSayingSyncBit |
                                                     kNetworkDiagBit |
                                                     kNetworkStateChangedBit |
                                                     kSetupPortalStartBit;
static constexpr EventBits_t kSetupPortalRetryWakeBits =
    kNetworkSyncWakeBits & ~kSetupPortalStartBit;
static_assert((kSetupPortalRetryWakeBits & kSetupPortalStartBit) == 0,
              "setup portal retry wait must ignore its pending level bit");
static constexpr time_t kBootWeatherRefreshDelaySec = 10;
static constexpr time_t kBootSayingRefreshDelaySec = 25;
static constexpr time_t kBootHttpsInterRequestGapSec = 8;
static constexpr uint32_t kBootHttpsMemoryRetryMs = 10000;
static constexpr uint32_t kProvisioningFeedbackWaitMs = 30000;
static constexpr uint32_t kProvisioningFeedbackPollMs = 100;
static constexpr uint32_t kProvisioningFeedbackDisplayGraceMs = 750;
static_assert(kBootWeatherRefreshDelaySec > 0,
              "boot weather refresh delay must be positive");
static_assert(kBootSayingRefreshDelaySec > kBootWeatherRefreshDelaySec,
              "boot saying refresh delay must stay after boot weather refresh");
static_assert(kBootHttpsInterRequestGapSec > 0,
              "boot HTTPS inter-request gap must be positive");
static_assert(kBootHttpsMemoryRetryMs >= 1000,
              "boot HTTPS memory retry must avoid a tight loop");
static_assert(kBootHttpsMemoryRetryMs % 1000 == 0,
              "boot HTTPS memory retry must convert exactly to seconds");
static_assert(kNetworkBootSyncGateWarningMs > 0,
              "boot sync gate warning delay must be positive");
static_assert(kProvisioningFeedbackWaitMs >= kProvisioningFeedbackDisplayGraceMs,
              "provisioning feedback wait must cover its display grace");
static_assert(kProvisioningFeedbackPollMs > 0,
              "provisioning feedback poll delay must be positive");
static_assert(kNetworkDiagOtaLine == kNetworkDiagLineCount - 1,
              "network service diagnostics line mapping must match diagnostics line count");
static constexpr const char *kNetworkDiagIpLocationWifiStartFailed = "IP定位: WiFi启动失败";
static constexpr const char *kNetworkDiagIpLocationPowerLockUnavailable = "IP定位: 系统繁忙";
static constexpr const char *kNetworkDiagIpLocationWifiConnectTimeout = "IP定位: WiFi连接超时";
static constexpr const char *kNetworkSyncWeatherKeyMissing = "未配置 API Key";
static constexpr const char *kNetworkSyncLowBatterySkipped = "电量低，已跳过";
#define NETWORK_BOOT_REFRESH_SCHEDULED_FORMAT "boot network refresh scheduled: weather=%d saying=%d"
#define NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT \
    "background boot HTTPS deferred: internal_free=%u internal_largest=%u dma_largest=%u"
#define NETWORK_BOOT_SAYING_STAGGERED_FORMAT \
    "boot daily saying deferred %lld seconds after weather"
#define NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT \
    "boot weather resource retry deferred %lld seconds"
#define NETWORK_NTP_RETRY_SCHEDULED_FORMAT \
    "ntp retry scheduled: delay=%llds time_valid=%d"
static constexpr const char *kNetworkDiagWifiOnLog = "wifi radio on for network diagnostics";
#define NETWORK_SYNC_WIFI_ON_FORMAT "wifi radio on for sync: ntp=%d weather=%d saying=%d boot_weather=%d boot_saying=%d"
static constexpr const char *kNetworkSyncWifiStartFailedLog = "wifi start failed during sync window";
static constexpr const char *kNetworkSyncPowerLockUnavailableLog =
    "network PM lock unavailable during sync window";
static constexpr const char *kNetworkSyncWifiConnectTimeoutLog = "wifi connect timeout during sync window";
static constexpr const char *kNetworkBootSyncGateWaitLog = "network sync waiting for boot connectivity task";
static constexpr const char *kProvisioningValidationFailedKeepPortalLog =
    "provisioning validation failed; setup portal remains active";
static constexpr const char *kSetupPortalStartFailedLog =
    "queued setup portal start failed; retrying";
static constexpr const char *kSetupPortalStartedLog =
    "queued setup portal start completed";
#define PROVISIONING_FEEDBACK_WAIT_DONE_FORMAT \
    "provisioning result feedback wait complete: seen=%d elapsed_ms=%u"
#define PROVISIONING_BACKGROUND_REFRESH_FORMAT \
    "provisioning background refresh scheduled: ntp=1 weather=%d saying=%d"

struct NetworkRuntimeAvailabilitySnapshot {
    bool have_wifi_creds;
    bool have_weather_key;
    bool offline_mode;
    bool low_battery_mode;
};

struct NetworkSyncRuntimeState {
    time_t next_ntp_retry_at = 0;
    time_t next_daily_ntp_at = 0;
    time_t boot_weather_due_at = 0;
    time_t boot_saying_due_at = 0;
    bool boot_ntp_due = false;
    bool daily_ntp_pending = false;
    bool boot_weather_due = false;
    bool boot_saying_due = false;
};

enum class SetupPortalStartResult {
    kNoRequest,
    kStarted,
    kRetryPending,
};

static NetworkRuntimeAvailabilitySnapshot capture_network_runtime_availability()
{
    const NetworkCredentialsAvailability credentials = network_credentials_availability();
    return {
        credentials.wifi_configured,
        credentials.weather_api_key_configured,
        offline_mode_enabled_load(),
        battery_low_mode_load(),
    };
}

static bool enabled_weather_data_page_exists()
{
    return is_work_page_enabled(kWorkPageWeatherClock) ||
           is_work_page_enabled(kWorkPageWeatherBoard);
}

static bool enabled_daily_saying_page_exists()
{
    return is_work_page_enabled(kWorkPageGallery);
}

static void wait_for_network_sync_event(uint32_t timeout_ms)
{
    app_event_group_wait_bits(kNetworkSyncWakeBits,
                              pdFALSE,
                              pdFALSE,
                              pdMS_TO_TICKS(timeout_ms));
}

static void wait_for_setup_portal_retry()
{
    // kSetupPortalStartBit remains set until the AP starts successfully. Do
    // not wait on that same level-triggered bit or the task will wake itself
    // immediately and spin through Wi-Fi start attempts without backoff.
    app_event_group_wait_bits(kSetupPortalRetryWakeBits,
                              pdFALSE,
                              pdFALSE,
                              pdMS_TO_TICKS(kNetworkShortRetryWaitMs));
}

static void wait_for_network_runtime_request()
{
    app_event_group_wait_bits(kNetworkSyncWakeBits,
                              pdFALSE,
                              pdFALSE,
                              portMAX_DELAY);
}

static void wait_for_ota_network_block_change()
{
    // Pending level-triggered sync bits remain queued while OTA owns HTTPS and
    // Wi-Fi. Wait only for the edge-like runtime-state bit so those requests do
    // not turn the protection branch into a busy loop.
    app_event_group_wait_bits(kNetworkStateChangedBit,
                              pdTRUE,
                              pdFALSE,
                              portMAX_DELAY);
}

static void schedule_ntp_retry(time_t *next_ntp_retry_at)
{
    if (!next_ntp_retry_at) {
        return;
    }
    const bool time_valid = is_system_time_plausible();
    const time_t delay_seconds = network_ntp_retry_delay_seconds(time_valid);
    time(next_ntp_retry_at);
    *next_ntp_retry_at += delay_seconds;
    ESP_LOGI(TAG,
             NETWORK_NTP_RETRY_SCHEDULED_FORMAT,
             static_cast<long long>(delay_seconds),
             time_valid);
}

static void update_ntp_retry_deadline(bool retry_required,
                                      time_t *next_ntp_retry_at)
{
    if (!next_ntp_retry_at) {
        return;
    }
    if (retry_required) {
        schedule_ntp_retry(next_ntp_retry_at);
    } else {
        *next_ntp_retry_at = 0;
    }
}

static bool weather_cache_current_hour(time_t now)
{
    return network_weather_cache_current_hour(
        now,
        get_last_weather_sync_time());
}

static bool saying_cache_current_day(time_t now)
{
    char saying[kDailySayingLen] = {};
    time_t last_sync_time = 0;
    if (!get_daily_saying_snapshot(saying, sizeof(saying), &last_sync_time)) {
        return false;
    }
    return network_daily_saying_cache_current_day(now, last_sync_time);
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

static void finish_network_radio_session(NetworkAwakeLockGuard &awake_lock,
                                         bool force_setup_portal = false)
{
    // Keep the CPU awake through esp_wifi_stop(), then service any deferred
    // close request after this session releases its final PM-lock ownership.
    if (awake_lock.locked()) {
        stop_wifi_radio(force_setup_portal);
    }
    awake_lock.release();
    service_wifi_radio_stop_when_idle();
}

static SetupPortalStartResult service_setup_portal_start_request()
{
    if (!setup_portal_start_requested()) {
        return SetupPortalStartResult::kNoRequest;
    }
    if (!start_wifi_radio(true)) {
        ESP_LOGW(TAG, "%s", kSetupPortalStartFailedLog);
        return SetupPortalStartResult::kRetryPending;
    }
    app_event_group_clear_bits(kSetupPortalStartBit);
    settings_page_clear();
    network_diag_page_clear();
    info_page_clear();
    notify_ui_task();
    ESP_LOGI(TAG, "%s", kSetupPortalStartedLog);
    return SetupPortalStartResult::kStarted;
}

static void wait_for_provisioning_result_feedback()
{
    const TickType_t started_at = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(kProvisioningFeedbackWaitMs);
    bool seen = false;
    while (setup_portal_active_load() &&
           xTaskGetTickCount() - started_at < timeout_ticks) {
        if (wifi_portal_save_feedback_seen_load()) {
            seen = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kProvisioningFeedbackPollMs));
    }
    if (seen) {
        vTaskDelay(pdMS_TO_TICKS(kProvisioningFeedbackDisplayGraceMs));
    }
    const uint32_t elapsed_ms = static_cast<uint32_t>(
        (xTaskGetTickCount() - started_at) * portTICK_PERIOD_MS);
    ESP_LOGI(TAG,
             PROVISIONING_FEEDBACK_WAIT_DONE_FORMAT,
             seen,
             static_cast<unsigned>(elapsed_ms));
}

static void schedule_background_refresh_after_provisioning(
    bool have_weather_key,
    NetworkSyncRuntimeState &runtime)
{
    time_t now = 0;
    time(&now);
    runtime.boot_ntp_due = true;
    runtime.boot_weather_due = have_weather_key && enabled_weather_data_page_exists();
    runtime.boot_saying_due = enabled_daily_saying_page_exists();
    runtime.boot_weather_due_at = now + kBootWeatherRefreshDelaySec;
    runtime.boot_saying_due_at = now + kBootSayingRefreshDelaySec;
    runtime.next_ntp_retry_at = 0;
    runtime.daily_ntp_pending = false;
    runtime.next_daily_ntp_at = 0;
    ESP_LOGI(TAG,
             PROVISIONING_BACKGROUND_REFRESH_FORMAT,
             runtime.boot_weather_due,
             runtime.boot_saying_due);
}

static void keep_setup_portal_after_provisioning_failure(
    NetworkAwakeLockGuard &awake_lock,
    WifiPortalSaveResult result)
{
    (void)prepare_setup_portal_result_delivery();
    wifi_portal_save_result_store(result);
    // The AP and HTTP server must remain available so the phone can display
    // the stored error and submit corrected credentials. Only release the CPU
    // awake lock owned by this validation attempt.
    awake_lock.release();
    ESP_LOGW(TAG, "%s", kProvisioningValidationFailedKeepPortalLog);
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

static bool defer_automatic_boot_https_for_memory(NetworkSyncSchedule *schedule,
                                                  const NetworkSyncRequestSnapshot &requests,
                                                  time_t now,
                                                  NetworkSyncRuntimeState &runtime)
{
    if (!schedule) {
        return false;
    }
    NetworkBootHttpsDeferralInput input = {};
    input.now = now;
    input.retry_delay_seconds =
        static_cast<time_t>(kBootHttpsMemoryRetryMs / 1000);
    input.provisioning_sync_due = requests.provisioning;
    input.manual_weather_due = requests.manual_weather;
    input.manual_saying_due = requests.manual_saying;
    if (!network_automatic_boot_https_pending(*schedule, input)) {
        return false;
    }
    const NetworkHttpsMemorySnapshot memory = capture_network_https_memory_snapshot();
    input.memory_allowed = network_automatic_boot_https_allowed(
        startup_screen_active(),
        esp_timer_get_time(),
        memory.internal_free,
        memory.internal_largest,
        memory.dma_largest);
    NetworkBootHttpsDeferralResult result =
        calculate_network_boot_https_deferral(*schedule, input);
    if (!result.deferred) {
        return false;
    }
    ESP_LOGW(TAG,
             NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT,
             static_cast<unsigned>(memory.internal_free),
             static_cast<unsigned>(memory.internal_largest),
             static_cast<unsigned>(memory.dma_largest));
    *schedule = result.schedule;
    if (result.weather_deferred) {
        runtime.boot_weather_due_at = result.retry_at;
    }
    if (result.saying_deferred) {
        runtime.boot_saying_due_at = result.retry_at;
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
        update_ntp_retry_deadline(schedule.ntp_retry_required,
                                  next_ntp_retry_at);
    }
}

static void finalize_failed_network_sync_attempt(
    const NetworkSyncSchedule &schedule,
    const NetworkSyncRequestSnapshot &requests,
    NetworkSyncRuntimeState &runtime)
{
    finalize_failed_network_sync_window(schedule,
                                        requests,
                                        &runtime.boot_weather_due,
                                        &runtime.boot_saying_due,
                                        &runtime.next_ntp_retry_at);
    stagger_boot_saying_after_weather(schedule,
                                      runtime.boot_saying_due,
                                      &runtime.boot_saying_due_at);
}

static void finish_failed_network_sync_session(
    const NetworkSyncSchedule &schedule,
    const NetworkSyncRequestSnapshot &requests,
    NetworkSyncRuntimeState &runtime,
    NetworkAwakeLockGuard &awake_lock,
    WifiPortalSaveResult provisioning_result)
{
    finalize_failed_network_sync_attempt(schedule, requests, runtime);
    if (requests.provisioning) {
        keep_setup_portal_after_provisioning_failure(awake_lock,
                                                     provisioning_result);
        return;
    }
    finish_network_radio_session(awake_lock);
}

static void execute_connected_sync_window(const NetworkSyncSchedule &schedule,
                                          const NetworkSyncRequestSnapshot &requests,
                                          NetworkSyncRuntimeState &runtime)
{
    bool ntp_ok = false;
    bool weather_ok = false;
    bool weather_resource_deferred = false;
    bool saying_ok = false;
    NetworkDisplayDmaGuard display_guard(schedule.weather_due || schedule.saying_due);
    if (schedule.ntp_due) {
        if (perform_ntp_sync()) {
            ntp_ok = true;
            runtime.boot_ntp_due = false;
            runtime.daily_ntp_pending = false;
            runtime.next_ntp_retry_at = 0;
            runtime.next_daily_ntp_at = next_local_midnight_time(time(nullptr));
        } else {
            update_ntp_retry_deadline(schedule.ntp_retry_required,
                                      &runtime.next_ntp_retry_at);
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
    runtime.boot_weather_due = network_boot_weather_due_after_update(
        runtime.boot_weather_due,
        schedule.boot_weather_ready,
        weather_resource_deferred);
    if (schedule.boot_weather_ready && weather_resource_deferred) {
        time_t now = 0;
        time(&now);
        runtime.boot_weather_due_at =
            now + static_cast<time_t>(kBootHttpsMemoryRetryMs / 1000);
        ESP_LOGI(TAG,
                 NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT,
                 static_cast<long long>(kBootHttpsMemoryRetryMs / 1000));
    }
    clear_ready_boot_sync_flags(false,
                                schedule.boot_saying_ready,
                                nullptr,
                                &runtime.boot_saying_due);
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
    if (!awake_lock.locked()) {
        set_network_diag_unavailable(kNetworkDiagIpLocationPowerLockUnavailable);
    } else if (!start_wifi_radio(false)) {
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiStartFailed);
    } else if (!wait_for_wifi_connected(kNetworkWifiConnectTimeoutMs)) {
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiConnectTimeout);
    } else {
        run_network_diagnostic_checks();
    }
    finish_network_radio_session(awake_lock);
    network_diag_finish();
    finish_network_diagnostics_sync();
}

static void wait_for_boot_sync_completion()
{
    EventBits_t bits = app_event_group_wait_bits(kBootSyncDoneBit,
                                                 pdFALSE,
                                                 pdTRUE,
                                                 pdMS_TO_TICKS(kNetworkBootSyncGateWarningMs));
    if ((bits & kBootSyncDoneBit) != 0) {
        return;
    }
    ESP_LOGW(TAG, "%s", kNetworkBootSyncGateWaitLog);
    app_event_group_wait_bits(kBootSyncDoneBit,
                              pdFALSE,
                              pdTRUE,
                              portMAX_DELAY);
}

void network_sync_task(void *)
{
    wait_for_boot_sync_completion();
    vTaskDelay(pdMS_TO_TICKS(kNetworkTaskStartupDelayMs));
    EventBits_t initial_bits = app_event_group_get_bits();
    NetworkSyncRuntimeState sync_runtime;
    sync_runtime.boot_ntp_due = (initial_bits & kTimeSyncedBit) == 0;
    time_t boot_schedule_now = 0;
    time(&boot_schedule_now);
    sync_runtime.next_daily_ntp_at = sync_runtime.boot_ntp_due
                                         ? 0
                                         : next_local_midnight_time(boot_schedule_now);
    const NetworkRuntimeAvailabilitySnapshot initial_runtime =
        capture_network_runtime_availability();
    sync_runtime.boot_weather_due = initial_runtime.have_wifi_creds &&
                                    initial_runtime.have_weather_key &&
                                    !initial_runtime.offline_mode &&
                                    !initial_runtime.low_battery_mode &&
                                    enabled_weather_data_page_exists();
    sync_runtime.boot_saying_due = initial_runtime.have_wifi_creds &&
                                   !initial_runtime.offline_mode &&
                                   !initial_runtime.low_battery_mode &&
                                   enabled_daily_saying_page_exists();
    sync_runtime.boot_weather_due_at =
        boot_schedule_now + kBootWeatherRefreshDelaySec;
    sync_runtime.boot_saying_due_at =
        boot_schedule_now + kBootSayingRefreshDelaySec;
    if (sync_runtime.boot_weather_due || sync_runtime.boot_saying_due) {
        ESP_LOGI(TAG,
                 NETWORK_BOOT_REFRESH_SCHEDULED_FORMAT,
                 sync_runtime.boot_weather_due,
                 sync_runtime.boot_saying_due);
    }

    for (;;) {
        // Consume only the edge-like state notification before reading the
        // latest runtime state. Sync request bits stay level-triggered.
        app_event_group_clear_bits(kNetworkStateChangedBit);
        NetworkSyncRequestSnapshot requests = snapshot_network_sync_requests();
        const NetworkRuntimeAvailabilitySnapshot runtime =
            capture_network_runtime_availability();
        int ota_state = ota_runtime_state_load();
        if (ota_blocks_background_network_sync(ota_state)) {
            wait_for_ota_network_block_change();
            continue;
        }
        const SetupPortalStartResult setup_portal_start =
            service_setup_portal_start_request();
        if (setup_portal_start != SetupPortalStartResult::kNoRequest) {
            if (setup_portal_start == SetupPortalStartResult::kRetryPending) {
                wait_for_setup_portal_retry();
            } else {
                wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            }
            continue;
        }
        if (runtime.offline_mode) {
            sync_runtime.boot_weather_due = false;
            sync_runtime.boot_saying_due = false;
            finish_offline_network_requests(requests);
            wait_for_network_runtime_request();
            continue;
        }
        if (!runtime.have_wifi_creds) {
            sync_runtime.boot_weather_due = false;
            sync_runtime.boot_saying_due = false;
            finish_unconfigured_network_requests(requests);
            wait_for_network_runtime_request();
            continue;
        }
        if (requests.manual_weather && !runtime.have_weather_key) {
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

        time_t now;
        time(&now);
        struct tm local = {};
        bool time_valid = localtime_r(&now, &local) && is_tm_plausible(local);
        if (!sync_runtime.daily_ntp_pending &&
            sync_runtime.next_daily_ntp_at > 0 &&
            now >= sync_runtime.next_daily_ntp_at) {
            sync_runtime.daily_ntp_pending = true;
            sync_runtime.next_daily_ntp_at = 0;
        }
        if (!sync_runtime.daily_ntp_pending &&
            sync_runtime.next_daily_ntp_at == 0 &&
            !sync_runtime.boot_ntp_due && time_valid) {
            sync_runtime.next_daily_ntp_at = next_local_midnight_time(now);
        }
        // A short boot request may obtain current conditions while forecast or
        // air quality times out. Keep the staggered full refresh scheduled in
        // that partial state so the weather board is ready before first entry.
        if (sync_runtime.boot_weather_due &&
            weather_cache_current_hour(now) &&
            weather_extended_data_ready()) {
            sync_runtime.boot_weather_due = false;
        }
        if (sync_runtime.boot_saying_due && saying_cache_current_day(now)) {
            sync_runtime.boot_saying_due = false;
        }
        if (runtime.low_battery_mode) {
            sync_runtime.boot_weather_due = false;
            sync_runtime.boot_saying_due = false;
        }
        NetworkSyncScheduleInput schedule_input = {};
        schedule_input.now = now;
        schedule_input.next_ntp_retry_at = sync_runtime.next_ntp_retry_at;
        schedule_input.boot_weather_due_at = sync_runtime.boot_weather_due_at;
        schedule_input.boot_saying_due_at = sync_runtime.boot_saying_due_at;
        schedule_input.have_weather_key = runtime.have_weather_key;
        schedule_input.low_battery_mode = runtime.low_battery_mode;
        schedule_input.provisioning_sync_due = requests.provisioning;
        schedule_input.manual_ntp_due = requests.manual_ntp;
        schedule_input.manual_weather_due = requests.manual_weather;
        schedule_input.manual_saying_due = requests.manual_saying;
        schedule_input.boot_ntp_due = sync_runtime.boot_ntp_due;
        schedule_input.daily_ntp_due = sync_runtime.daily_ntp_pending;
        schedule_input.boot_weather_due = sync_runtime.boot_weather_due;
        schedule_input.boot_saying_due = sync_runtime.boot_saying_due;
        NetworkSyncSchedule schedule = calculate_network_sync_schedule(schedule_input);
        bool boot_https_memory_deferred = defer_automatic_boot_https_for_memory(
            &schedule,
            requests,
            now,
            sync_runtime);
        if (runtime.low_battery_mode && requests.manual_weather) {
            finish_settings_sync_and_clear_bit(kSettingsSyncWeather, kNetworkSyncLowBatterySkipped, kManualWeatherSyncBit);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }
        if (runtime.low_battery_mode && requests.manual_saying) {
            finish_settings_sync_and_clear_bit(kSettingsSyncSaying, kNetworkSyncLowBatterySkipped, kManualSayingSyncBit);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }

        if (!schedule.ntp_due && !schedule.weather_due && !schedule.saying_due) {
            uint32_t wait_ms = boot_https_memory_deferred
                                   ? kBootHttpsMemoryRetryMs
                                   : network_idle_wait_ms(now,
                                                          schedule.next_boot_due_at,
                                                          sync_runtime.next_ntp_retry_at,
                                                          sync_runtime.next_daily_ntp_at);
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
        if (!awake_lock.locked() || !start_wifi_radio(requests.provisioning)) {
            ESP_LOGW(TAG,
                     "%s",
                     awake_lock.locked()
                         ? kNetworkSyncWifiStartFailedLog
                         : kNetworkSyncPowerLockUnavailableLog);
            finish_failed_network_sync_session(
                schedule,
                requests,
                sync_runtime,
                awake_lock,
                WifiPortalSaveResult::kWifiConnectionFailed);
            wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            continue;
        }
        if (wait_for_wifi_connected(kNetworkWifiConnectTimeoutMs)) {
            if (requests.provisioning) {
                WifiPortalSaveResult validation_result =
                    validate_saved_provisioning_weather_configuration();
                if (validation_result != WifiPortalSaveResult::kSuccess) {
                    finish_failed_network_sync_session(schedule,
                                                       requests,
                                                       sync_runtime,
                                                       awake_lock,
                                                       validation_result);
                    wait_for_network_sync_event(kNetworkShortRetryWaitMs);
                    continue;
                }
                (void)prepare_setup_portal_result_delivery();
                wifi_portal_save_result_store(WifiPortalSaveResult::kSuccess);
                app_event_group_clear_bits(kProvisioningSyncBit);
                wait_for_provisioning_result_feedback();
                finish_network_radio_session(awake_lock, true);
                schedule_background_refresh_after_provisioning(
                    runtime.have_weather_key,
                    sync_runtime);
                continue;
            }
            execute_connected_sync_window(schedule,
                                          requests,
                                          sync_runtime);
            stagger_boot_saying_after_weather(schedule,
                                              sync_runtime.boot_saying_due,
                                              &sync_runtime.boot_saying_due_at);
        } else {
            ESP_LOGW(TAG, "%s", kNetworkSyncWifiConnectTimeoutLog);
            finish_failed_network_sync_session(
                schedule,
                requests,
                sync_runtime,
                awake_lock,
                WifiPortalSaveResult::kWifiConnectionFailed);
            if (requests.provisioning) {
                wait_for_network_sync_event(kNetworkShortRetryWaitMs);
            }
            continue;
        }
        finish_network_radio_session(awake_lock, requests.provisioning);
    }
}
