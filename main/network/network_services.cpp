// 调度 NTP、天气、预警、每日文字和 OTA 等联网同步流程。
#include "network_sync_task.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "daily_saying_state.h"
#include "daily_saying_service.h"
#include "ota_runtime_state.h"

#include "network_https_resources.h"
#include "network_cache_policy.h"
#include "network_diagnostics.h"
#include "ntp_services.h"
#include "network_provisioning_session.h"
#include "provisioning_validation.h"
#include "network_diagnostics_catalog.h"
#include "network_sync_requests.h"
#include "network_sync_runtime.h"
#include "network_sync_schedule.h"
#include "network_sync_wait.h"
#include "network_task_guards.h"
#include "sensor_time.h"
#include "setup_portal_control.h"
#include "startup_state.h"
#include "ui_work_page_catalog.h"
#include "weather_state.h"
#include "weather_update.h"
#include "wifi_idle_stop_policy.h"
#include "wifi_portal_state.h"
#include "wifi_radio_services.h"
#include "wifi_radio_state.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <limits.h>

static constexpr uint32_t kNetworkShortRetryWaitMs = 1000;
static constexpr uint32_t kNetworkWifiConnectTimeoutMs = 45000;
static constexpr uint32_t kNetworkTaskStartupDelayMs = 2500;
static constexpr time_t kBootWeatherRefreshDelaySec = 10;
static constexpr time_t kBootSayingRefreshDelaySec = 25;
static constexpr time_t kBootHttpsInterRequestGapSec = 8;
static_assert(kBootWeatherRefreshDelaySec > 0,
              "boot weather refresh delay must be positive");
static_assert(kBootSayingRefreshDelaySec > kBootWeatherRefreshDelaySec,
              "boot saying refresh delay must stay after boot weather refresh");
static_assert(kBootHttpsInterRequestGapSec > 0,
              "boot HTTPS inter-request gap must be positive");
static_assert(kNetworkDiagOtaLine == kNetworkDiagLineCount - 1,
              "network service diagnostics line mapping must match diagnostics line count");
static constexpr const char *kNetworkDiagIpLocationWifiStartFailed = "IP定位: WiFi启动失败";
static constexpr const char *kNetworkDiagIpLocationPowerLockUnavailable = "IP定位: 系统繁忙";
static constexpr const char *kNetworkDiagIpLocationWifiConnectTimeout = "IP定位: WiFi连接超时";
static constexpr const char *kNetworkSyncWeatherKeyMissing = "未配置 API Key";
#define NETWORK_BOOT_REFRESH_SCHEDULED_FORMAT "boot network refresh scheduled: weather=%d saying=%d"
#define NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT \
    "background boot HTTPS deferred: delay=%llds deferrals=%u internal_free=%u internal_largest=%u dma_largest=%u"
#define NETWORK_BOOT_SAYING_STAGGERED_FORMAT \
    "boot daily saying deferred %lld seconds after weather"
#define NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT \
    "boot weather resource retry deferred %lld seconds deferrals=%u"
#define NETWORK_NTP_RETRY_SCHEDULED_FORMAT \
    "ntp retry scheduled: delay=%llds time_valid=%d failures=%u"
static constexpr const char *kNetworkDiagWifiOnLog = "wifi radio on for network diagnostics";
#define NETWORK_SYNC_WIFI_ON_FORMAT "wifi radio on for sync: ntp=%d weather=%d saying=%d boot_weather=%d boot_saying=%d"
static constexpr const char *kNetworkSyncWifiStartFailedLog = "wifi start failed during sync window";
static constexpr const char *kNetworkSyncPowerLockUnavailableLog =
    "network PM lock unavailable during sync window";
static constexpr const char *kNetworkSyncWifiConnectTimeoutLog = "wifi connect timeout during sync window";
static constexpr const char *kNetworkSyncContextChangedLog =
    "network sync deferred after runtime state change";
#define PROVISIONING_BACKGROUND_REFRESH_FORMAT \
    "provisioning background refresh scheduled: ntp=1 weather=%d saying=%d"

struct NetworkSyncRuntimeState {
    time_t next_ntp_retry_at = 0;
    time_t next_daily_ntp_at = 0;
    time_t boot_weather_due_at = 0;
    time_t boot_saying_due_at = 0;
    uint8_t ntp_retry_failures = 0;
    uint8_t boot_https_memory_deferrals = 0;
    uint8_t boot_weather_resource_deferrals = 0;
    uint8_t wifi_stop_retry_failures = 0;
    bool boot_ntp_due = false;
    bool daily_ntp_pending = false;
    bool boot_weather_due = false;
    bool boot_saying_due = false;
};

static WorkPageDataRequirements capture_network_refresh_page_availability()
{
    return enabled_work_page_data_requirements(
        work_page_enabled_mask_load());
}

static_assert(kWorkPageCount <= 8,
              "network refresh page snapshot must fit the enabled-page mask");

static void schedule_ntp_retry(time_t *next_ntp_retry_at,
                               uint8_t *ntp_retry_failures)
{
    if (!next_ntp_retry_at || !ntp_retry_failures) {
        return;
    }
    if (*ntp_retry_failures < UINT8_MAX) {
        ++(*ntp_retry_failures);
    }
    const bool time_valid = is_system_time_plausible();
    const time_t delay_seconds = network_ntp_retry_delay_seconds(
        time_valid,
        *ntp_retry_failures);
    time(next_ntp_retry_at);
    *next_ntp_retry_at += delay_seconds;
    ESP_LOGI(TAG,
             NETWORK_NTP_RETRY_SCHEDULED_FORMAT,
             static_cast<long long>(delay_seconds),
             time_valid,
             static_cast<unsigned>(*ntp_retry_failures));
}

static void update_ntp_retry_deadline(bool retry_required,
                                      time_t *next_ntp_retry_at,
                                      uint8_t *ntp_retry_failures)
{
    if (!next_ntp_retry_at || !ntp_retry_failures) {
        return;
    }
    if (retry_required) {
        schedule_ntp_retry(next_ntp_retry_at, ntp_retry_failures);
    } else {
        *next_ntp_retry_at = 0;
        *ntp_retry_failures = 0;
    }
}

static bool weather_cache_complete_for_current_hour(time_t now)
{
    WeatherCacheStatusSnapshot snapshot;
    return weather_cache_status_snapshot_load(&snapshot) &&
           network_weather_cache_current_hour(now, snapshot.last_sync_time) &&
           snapshot.extended_data_ready;
}

static bool saying_cache_current_day(time_t now)
{
    DailySayingCacheSnapshot snapshot;
    if (!daily_saying_cache_snapshot_load(&snapshot) || !snapshot.available) {
        return false;
    }
    return network_daily_saying_cache_current_day(now, snapshot.last_sync_time);
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

static void cancel_disabled_page_boot_refreshes(NetworkSyncRuntimeState &runtime)
{
    if (!runtime.boot_weather_due && !runtime.boot_saying_due) {
        return;
    }
    const WorkPageDataRequirements pages =
        capture_network_refresh_page_availability();
    if (runtime.boot_weather_due && !pages.weather) {
        runtime.boot_weather_due = false;
        runtime.boot_weather_due_at = 0;
    }
    if (runtime.boot_saying_due && !pages.daily_saying) {
        runtime.boot_saying_due = false;
        runtime.boot_saying_due_at = 0;
    }
}

static bool automatic_boot_refresh_page_disabled(
    const NetworkSyncSchedule &schedule,
    const NetworkSyncRequestSnapshot &requests)
{
    if (requests.provisioning ||
        (!schedule.boot_weather_ready && !schedule.boot_saying_ready)) {
        return false;
    }
    NetworkAutomaticBootPageInput input = {};
    const WorkPageDataRequirements pages =
        capture_network_refresh_page_availability();
    input.provisioning_sync_due = requests.provisioning;
    input.explicit_weather_due = requests.weather_due();
    input.explicit_saying_due = requests.saying_due();
    input.weather_page_enabled = pages.weather;
    input.saying_page_enabled = pages.daily_saying;
    return network_automatic_boot_refresh_page_disabled(schedule, input);
}

static void finish_network_radio_session(NetworkAwakeLockGuard &awake_lock,
                                         bool force_setup_portal = false)
{
    // Keep the CPU awake through esp_wifi_stop(). If another protected owner
    // blocks this attempt, retain a real close request until that owner exits.
    if (awake_lock.locked()) {
        stop_wifi_radio(force_setup_portal);
        awake_lock.release();
        request_wifi_radio_stop_when_idle();
    } else {
        // This caller never acquired session ownership, so it must not create a
        // new stop request; it may only service one left by an earlier owner.
        service_wifi_radio_stop_when_idle();
    }
}

static void schedule_background_refresh_after_provisioning(
    bool have_weather_key,
    NetworkSyncRuntimeState &runtime)
{
    time_t now = 0;
    time(&now);
    const WorkPageDataRequirements pages =
        capture_network_refresh_page_availability();
    runtime.boot_ntp_due = true;
    runtime.boot_weather_due = have_weather_key && pages.weather;
    runtime.boot_saying_due = pages.daily_saying;
    runtime.boot_weather_due_at = now + kBootWeatherRefreshDelaySec;
    runtime.boot_saying_due_at = now + kBootSayingRefreshDelaySec;
    runtime.next_ntp_retry_at = 0;
    runtime.ntp_retry_failures = 0;
    runtime.boot_https_memory_deferrals = 0;
    runtime.boot_weather_resource_deferrals = 0;
    runtime.daily_ntp_pending = false;
    runtime.next_daily_ntp_at = 0;
    ESP_LOGI(TAG,
             PROVISIONING_BACKGROUND_REFRESH_FORMAT,
             runtime.boot_weather_due,
             runtime.boot_saying_due);
}

static bool settle_between_network_operations(bool more_work_pending)
{
    if (!more_work_pending) {
        return true;
    }
    // The preceding client has already released its TLS buffers. Give the
    // allocator and UI task a scheduling window before the next operation.
    // The first minute uses a longer gap because boot UI, sensor and cache
    // initialization still compete for internal/DMA memory.
    const bool startup_pressure = network_startup_pressure_window_active(
        startup_screen_active(),
        esp_timer_get_time());
    return wait_for_network_sync_settle(
        network_inter_operation_settle_delay_ms(startup_pressure));
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
    input.provisioning_sync_due = requests.provisioning;
    input.manual_weather_due = requests.weather_due();
    input.manual_saying_due = requests.saying_due();
    if (!network_automatic_boot_https_pending(*schedule, input)) {
        if (!runtime.boot_weather_due && !runtime.boot_saying_due) {
            runtime.boot_https_memory_deferrals = 0;
        }
        return false;
    }
    const NetworkHttpsMemorySnapshot memory = capture_network_https_memory_snapshot();
    input.memory_allowed = network_automatic_boot_https_allowed(
        startup_screen_active(),
        esp_timer_get_time(),
        memory.internal_free,
        memory.internal_largest,
        memory.dma_largest);
    if (input.memory_allowed) {
        runtime.boot_https_memory_deferrals = 0;
        return false;
    }
    if (runtime.boot_https_memory_deferrals < UINT8_MAX) {
        ++runtime.boot_https_memory_deferrals;
    }
    input.retry_delay_seconds =
        network_boot_https_memory_retry_delay_seconds(
            runtime.boot_https_memory_deferrals);
    NetworkBootHttpsDeferralResult result =
        calculate_network_boot_https_deferral(*schedule, input);
    if (!result.deferred) {
        runtime.boot_https_memory_deferrals = 0;
        return false;
    }
    ESP_LOGW(TAG,
             NETWORK_BOOT_HTTPS_MEMORY_DEFERRED_FORMAT,
             static_cast<long long>(input.retry_delay_seconds),
             static_cast<unsigned>(runtime.boot_https_memory_deferrals),
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
                                                time_t *next_ntp_retry_at,
                                                uint8_t *ntp_retry_failures)
{
    clear_ready_boot_sync_flags(schedule.boot_weather_ready,
                                schedule.boot_saying_ready,
                                boot_weather_due,
                                boot_saying_due);
    finish_failed_sync_requests(requests);
    if (schedule.ntp_due) {
        update_ntp_retry_deadline(schedule.ntp_retry_required,
                                  next_ntp_retry_at,
                                  ntp_retry_failures);
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
                                        &runtime.next_ntp_retry_at,
                                        &runtime.ntp_retry_failures);
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

static bool execute_connected_sync_window(const NetworkSyncSchedule &schedule,
                                          const NetworkSyncRequestSnapshot &requests,
                                          NetworkSyncRuntimeState &runtime)
{
    bool ntp_ok = false;
    bool weather_ok = false;
    bool weather_resource_deferred = false;
    bool saying_ok = false;
    NetworkDisplayDmaGuard display_guard(schedule.weather_due || schedule.saying_due);
    if (schedule.ntp_due) {
        const bool ntp_succeeded = perform_ntp_sync();
        if (!network_sync_continuation_allowed()) {
            return false;
        }
        if (ntp_succeeded) {
            ntp_ok = true;
            runtime.boot_ntp_due = false;
            runtime.daily_ntp_pending = false;
            runtime.next_ntp_retry_at = 0;
            runtime.ntp_retry_failures = 0;
            runtime.next_daily_ntp_at = next_local_midnight_time(time(nullptr));
        } else {
            update_ntp_retry_deadline(schedule.ntp_retry_required,
                                      &runtime.next_ntp_retry_at,
                                      &runtime.ntp_retry_failures);
        }
        if (!settle_between_network_operations(schedule.weather_due ||
                                               schedule.saying_due)) {
            return false;
        }
    }
    if (schedule.weather_due) {
        WeatherUpdateResult result = perform_weather_update();
        if (!network_sync_continuation_allowed()) {
            return false;
        }
        weather_ok = result == WeatherUpdateResult::kSuccess;
        weather_resource_deferred = result == WeatherUpdateResult::kResourceDeferred;
        if (!settle_between_network_operations(schedule.saying_due)) {
            return false;
        }
    }
    if (schedule.saying_due) {
        saying_ok = perform_daily_saying_update();
        if (!network_sync_continuation_allowed()) {
            return false;
        }
    }
    runtime.boot_weather_due = network_boot_weather_due_after_update(
        runtime.boot_weather_due,
        schedule.boot_weather_ready,
        weather_resource_deferred);
    if (schedule.boot_weather_ready) {
        if (weather_resource_deferred) {
            if (runtime.boot_weather_resource_deferrals < UINT8_MAX) {
                ++runtime.boot_weather_resource_deferrals;
            }
            const time_t retry_delay_seconds =
                network_boot_https_memory_retry_delay_seconds(
                    runtime.boot_weather_resource_deferrals);
            time_t now = 0;
            time(&now);
            runtime.boot_weather_due_at = now + retry_delay_seconds;
            ESP_LOGI(
                TAG,
                NETWORK_BOOT_WEATHER_RESOURCE_RETRY_FORMAT,
                static_cast<long long>(retry_delay_seconds),
                static_cast<unsigned>(runtime.boot_weather_resource_deferrals));
        } else {
            runtime.boot_weather_resource_deferrals = 0;
        }
    }
    clear_ready_boot_sync_flags(false,
                                schedule.boot_saying_ready,
                                nullptr,
                                &runtime.boot_saying_due);
    finish_successful_sync_requests(requests,
                                    ntp_ok,
                                    weather_ok,
                                    saying_ok);
    return true;
}

static bool execute_network_diagnostics_window(
    const NetworkSyncAvailability &scheduled_runtime)
{
    ESP_LOGI(TAG, "%s", kNetworkDiagWifiOnLog);
    NetworkAwakeLockGuard awake_lock;
    network_diag_begin();
    if (!awake_lock.locked()) {
        set_network_diag_unavailable(kNetworkDiagIpLocationPowerLockUnavailable);
    } else if (!start_wifi_radio(false)) {
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiStartFailed);
    } else {
        const NetworkSyncConnectionWaitResult connection_wait =
            wait_for_valid_network_sync_connection(scheduled_runtime,
                                                   kNetworkDiagBit,
                                                   kNetworkWifiConnectTimeoutMs);
        if (connection_wait == NetworkSyncConnectionWaitResult::kRuntimeChanged) {
            ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
            finish_network_radio_session(awake_lock);
            return false;
        }
        if (connection_wait == NetworkSyncConnectionWaitResult::kConnected) {
            if (!run_network_diagnostic_checks()) {
                ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
                finish_network_radio_session(awake_lock);
                return false;
            }
        } else {
            set_network_diag_unavailable(
                kNetworkDiagIpLocationWifiConnectTimeout);
        }
    }
    finish_network_radio_session(awake_lock);
    network_diag_finish();
    finish_network_diagnostics_sync();
    return true;
}

static void wait_for_network_runtime_or_wifi_stop_retry(
    WifiRadioIdleStopResult wifi_stop_result,
    uint8_t wifi_stop_retry_failures)
{
    if (wifi_stop_result == WifiRadioIdleStopResult::kRetryRequired) {
        wait_for_network_sync_event(
            wifi_idle_stop_retry_delay_ms(wifi_stop_retry_failures));
    } else {
        wait_for_network_runtime_request();
    }
}

void network_sync_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(kNetworkTaskStartupDelayMs));
    EventBits_t initial_bits = app_event_group_get_bits();
    NetworkSyncRuntimeState sync_runtime;
    sync_runtime.boot_ntp_due = (initial_bits & kTimeSyncedBit) == 0;
    time_t boot_schedule_now = 0;
    time(&boot_schedule_now);
    sync_runtime.next_daily_ntp_at = sync_runtime.boot_ntp_due
                                         ? 0
                                         : next_local_midnight_time(boot_schedule_now);
    const NetworkSyncAvailability initial_runtime =
        capture_network_runtime_availability();
    const WorkPageDataRequirements initial_pages =
        capture_network_refresh_page_availability();
    sync_runtime.boot_weather_due = initial_runtime.have_wifi_creds &&
                                    initial_runtime.have_weather_key &&
                                    !initial_runtime.offline_mode &&
                                    !initial_runtime.low_battery_mode &&
                                    initial_pages.weather;
    sync_runtime.boot_saying_due = initial_runtime.have_wifi_creds &&
                                   !initial_runtime.offline_mode &&
                                   !initial_runtime.low_battery_mode &&
                                   initial_pages.daily_saying;
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
        const WifiRadioIdleStopResult wifi_stop_result =
            service_wifi_radio_stop_when_idle();
        if (wifi_stop_result == WifiRadioIdleStopResult::kRetryRequired) {
            if (sync_runtime.wifi_stop_retry_failures < UINT8_MAX) {
                ++sync_runtime.wifi_stop_retry_failures;
            }
        } else {
            sync_runtime.wifi_stop_retry_failures = 0;
        }
        const SetupPortalStopResult setup_portal_stop =
            service_setup_portal_stop_request();
        if (setup_portal_stop != SetupPortalStopResult::kNoRequest) {
            if (setup_portal_stop == SetupPortalStopResult::kRetryPending) {
                wait_for_setup_portal_retry();
            }
            continue;
        }
        NetworkSyncRequestSnapshot requests = snapshot_network_sync_requests();
        const NetworkSyncAvailability runtime =
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
            sync_runtime.boot_weather_resource_deferrals = 0;
            finish_offline_network_requests(requests);
            wait_for_network_runtime_or_wifi_stop_retry(
                wifi_stop_result,
                sync_runtime.wifi_stop_retry_failures);
            continue;
        }
        if (!runtime.have_wifi_creds) {
            sync_runtime.boot_weather_due = false;
            sync_runtime.boot_saying_due = false;
            sync_runtime.boot_weather_resource_deferrals = 0;
            finish_unconfigured_network_requests(requests);
            wait_for_network_runtime_or_wifi_stop_retry(
                wifi_stop_result,
                sync_runtime.wifi_stop_retry_failures);
            continue;
        }
        if (requests.weather_due() && !runtime.have_weather_key) {
            if (requests.manual_weather) {
                finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                                   kNetworkSyncWeatherKeyMissing,
                                                   kManualWeatherSyncBit);
            }
            if (requests.visible_weather) {
                app_event_group_clear_bits(kVisibleWeatherSyncBit);
            }
        }
        // A diagnostics request may have been queued just before low-battery
        // mode became active. Finish nonessential requests before selecting a
        // network window, then resnapshot so cleared bits cannot run stale.
        if (runtime.low_battery_mode) {
            finish_low_battery_network_requests(requests);
            requests = snapshot_network_sync_requests();
        }
        if (setup_portal_active_load()) {
            if (!requests.provisioning) {
                wait_for_active_setup_portal_request();
                continue;
            }
            // A request can be raised between clearing stale work and actually
            // starting the AP. Preserve those level bits for later, but keep
            // this connected window exclusively owned by provisioning.
            requests = requests.provisioning_only();
        }

        if (requests.diagnostics) {
            const NetworkSyncAvailability diagnostics_runtime =
                capture_network_runtime_availability();
            if (network_sync_start_context_changed(runtime, diagnostics_runtime)) {
                ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
                continue;
            }
            if (!execute_network_diagnostics_window(diagnostics_runtime)) {
                continue;
            }
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
        // A page may be disabled after its startup prefetch was queued. Drop
        // only that automatic prefetch before it can power the Wi-Fi radio;
        // explicit settings requests remain independently level-triggered.
        cancel_disabled_page_boot_refreshes(sync_runtime);
        // A short boot request may obtain current conditions while forecast or
        // air quality times out. Keep the staggered full refresh scheduled in
        // that partial state so the weather board is ready before first entry.
        if (sync_runtime.boot_weather_due &&
            weather_cache_complete_for_current_hour(now)) {
            sync_runtime.boot_weather_due = false;
        }
        if (sync_runtime.boot_saying_due && saying_cache_current_day(now)) {
            sync_runtime.boot_saying_due = false;
        }
        if (runtime.low_battery_mode) {
            sync_runtime.boot_weather_due = false;
            sync_runtime.boot_saying_due = false;
        }
        if (!sync_runtime.boot_weather_due) {
            sync_runtime.boot_weather_resource_deferrals = 0;
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
        schedule_input.manual_weather_due = requests.weather_due();
        schedule_input.manual_saying_due = requests.saying_due();
        schedule_input.boot_ntp_due = sync_runtime.boot_ntp_due;
        schedule_input.daily_ntp_due = sync_runtime.daily_ntp_pending;
        schedule_input.boot_weather_due = sync_runtime.boot_weather_due;
        schedule_input.boot_saying_due = sync_runtime.boot_saying_due;
        NetworkSyncSchedule schedule = calculate_network_sync_schedule(schedule_input);
        defer_automatic_boot_https_for_memory(
            &schedule,
            requests,
            now,
            sync_runtime);

        if (!schedule.ntp_due && !schedule.weather_due && !schedule.saying_due) {
            uint32_t wait_ms = network_idle_wait_ms(
                now,
                schedule.next_boot_due_at,
                sync_runtime.next_ntp_retry_at,
                sync_runtime.next_daily_ntp_at);
            const uint32_t wifi_stop_retry_wait_ms =
                wifi_idle_stop_retry_delay_ms(
                    sync_runtime.wifi_stop_retry_failures);
            if (wifi_stop_result == WifiRadioIdleStopResult::kRetryRequired &&
                wait_ms > wifi_stop_retry_wait_ms) {
                wait_ms = wifi_stop_retry_wait_ms;
            }
            wait_for_network_sync_event(wait_ms);
            continue;
        }

        const NetworkSyncAvailability current_runtime =
            capture_network_runtime_availability();
        if (network_sync_start_context_changed(runtime, current_runtime)) {
            ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
            continue;
        }
        const EventBits_t scheduled_request_bits =
            network_sync_request_bits(requests);
        if (scheduled_request_bits != 0) {
            const EventBits_t current_request_bits = app_event_group_get_bits();
            if (network_request_snapshot_canceled(
                    scheduled_request_bits,
                    current_request_bits)) {
                continue;
            }
        }
        if (automatic_boot_refresh_page_disabled(schedule, requests)) {
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
        const NetworkSyncConnectionWaitResult connection_wait =
            wait_for_valid_network_sync_connection(current_runtime,
                                                   scheduled_request_bits,
                                                   kNetworkWifiConnectTimeoutMs);
        if (connection_wait == NetworkSyncConnectionWaitResult::kRuntimeChanged) {
            ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
            finish_network_radio_session(awake_lock);
            continue;
        }
        if (connection_wait == NetworkSyncConnectionWaitResult::kConnected) {
            if (automatic_boot_refresh_page_disabled(schedule, requests)) {
                finish_network_radio_session(awake_lock);
                continue;
            }
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
                publish_setup_portal_result(WifiPortalSaveResult::kSuccess);
                app_event_group_clear_bits(kProvisioningSyncBit);
                wait_for_provisioning_result_feedback();
                finish_network_radio_session(awake_lock, true);
                schedule_background_refresh_after_provisioning(
                    runtime.have_weather_key,
                    sync_runtime);
                continue;
            }
            if (!execute_connected_sync_window(schedule,
                                               requests,
                                               sync_runtime)) {
                ESP_LOGI(TAG, "%s", kNetworkSyncContextChangedLog);
                finish_network_radio_session(awake_lock);
                continue;
            }
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
