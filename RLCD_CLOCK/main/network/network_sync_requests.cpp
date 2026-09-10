// 统一网络同步请求快照、用户反馈和事件位清理顺序。
#include "network_sync_requests.h"
#include "network_sync_requests_internal.h"

#include "app_event_group.h"
#include "network_config_internal.h"
#include "network_diagnostics_internal.h"
#include "network_provisioning_session_internal.h"
#include "network_runtime_events.h"
#include "network_sync_request_generation.h"
#include "ui_settings_feedback.h"
#include "ui_i18n.h"
#include "ui_task_notify.h"
#include "wifi_portal_state.h"
#include "wifi_radio_services.h"
#include "wifi_radio_state.h"

#include "freertos/task.h"

#include <stdint.h>

namespace {

constexpr EventBits_t kNetworkRequestClearBits = kProvisioningSyncBit |
                                                 kManualNtpSyncBit |
                                                 kManualWeatherSyncBit |
                                                 kManualSayingSyncBit |
                                                 kVisibleWeatherSyncBit |
                                                 kVisibleSayingSyncBit |
                                                 kNetworkDiagBit |
                                                 kOtaCheckBit |
                                                 kOtaInstallBit;
constexpr const char *kNetworkRequestResetReason = "network request reset";
static_assert((kNetworkRequestClearBits & kManualWeatherSyncBit) != 0,
              "network request clear bits must include manual weather sync");
static_assert((kNetworkRequestClearBits & kOtaCheckBit) != 0 &&
                  (kNetworkRequestClearBits & kOtaInstallBit) != 0,
              "network request clear bits must include OTA request bits");
static_assert((kNetworkRequestClearBits & kNetworkStateChangedBit) == 0,
              "network runtime state notification is not a sync request");
constexpr UiTextId kNetworkStatusOfflineModeEnabled = UiTextId::NetworkDiagOfflineModeEnabled;
constexpr UiTextId kNetworkStatusWifiNotConfigured = UiTextId::NetworkWifiNotConfigured;
constexpr UiTextId kNetworkDiagIpLocationWifiNotConfigured = UiTextId::NetworkDiagIpWifiNotConfigured;
constexpr UiTextId kNetworkSyncTimeComplete = UiTextId::NetworkSyncTimeComplete;
constexpr UiTextId kNetworkSyncWeatherComplete = UiTextId::NetworkSyncWeatherComplete;
constexpr UiTextId kNetworkSyncSayingComplete = UiTextId::NetworkSyncSayingComplete;
constexpr UiTextId kNetworkSyncNetworkDiagComplete = UiTextId::NetworkSyncDiagnosticsComplete;
constexpr UiTextId kNetworkSyncTimeFailed = UiTextId::NetworkSyncTimeFailed;
constexpr UiTextId kNetworkSyncWeatherFailed = UiTextId::NetworkSyncWeatherFailed;
constexpr UiTextId kNetworkSyncSayingFailed = UiTextId::NetworkSyncSayingFailed;
constexpr UiTextId kNetworkSyncLowBatterySkipped = UiTextId::NetworkDiagStatusLowBatterySkipped;
constexpr UiTextId kNetworkSyncNetworkDiagCanceled = UiTextId::NetworkSyncDiagnosticsCanceled;
constexpr uint8_t kNetworkSnapshotYieldAttempts = 8;
static_assert(kNetworkSnapshotYieldAttempts > 1,
              "network snapshot contention must yield before sleeping");

void finish_requested_settings_sync(bool requested,
                                    SettingsSyncOp op,
                                    const char *status,
                                    EventBits_t bit,
                                    uint32_t request_generation,
                                    uint32_t settings_generation,
                                    bool retire_request)
{
    if (!requested) {
        return;
    }
    if (retire_request) {
        (void)retire_network_sync_request(bit, request_generation);
    }
    finish_settings_sync(op, settings_generation, status);
}

void finish_requested_manual_syncs(const NetworkSyncRequestSnapshot &requests,
                                   const char *status,
                                   bool clear_bits)
{
    finish_requested_settings_sync(requests.manual_ntp,
                                   kSettingsSyncNtp,
                                   status,
                                   kManualNtpSyncBit,
                                   requests.manual_ntp_generation,
                                   requests.manual_ntp_settings_generation,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_weather,
                                   kSettingsSyncWeather,
                                   status,
                                   kManualWeatherSyncBit,
                                   requests.manual_weather_generation,
                                   requests.manual_weather_settings_generation,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_saying,
                                   kSettingsSyncSaying,
                                   status,
                                   kManualSayingSyncBit,
                                   requests.manual_saying_generation,
                                   requests.manual_saying_settings_generation,
                                   clear_bits);
}

bool request_generations_equal(
    const NetworkSyncRequestGenerationSnapshot &lhs,
    const NetworkSyncRequestGenerationSnapshot &rhs)
{
    return lhs.manual_ntp == rhs.manual_ntp &&
           lhs.manual_weather == rhs.manual_weather &&
           lhs.manual_saying == rhs.manual_saying &&
           lhs.diagnostics == rhs.diagnostics;
}

bool settings_sync_requests_equal(
    const SettingsSyncRequestSnapshot &lhs,
    const SettingsSyncRequestSnapshot &rhs)
{
    return lhs.operation == rhs.operation &&
           lhs.generation == rhs.generation &&
           lhs.request_bit == rhs.request_bit &&
           lhs.request_generation == rhs.request_generation;
}

uint32_t matching_settings_generation(
    const SettingsSyncRequestSnapshot &settings,
    SettingsSyncOp operation,
    EventBits_t request_bit,
    uint32_t request_generation)
{
    return settings.operation == operation &&
                   settings.request_bit == request_bit &&
                   settings.request_generation == request_generation
               ? settings.generation
               : 0;
}

void clear_requested_visible_syncs(const NetworkSyncRequestSnapshot &requests)
{
    EventBits_t bits = 0;
    if (requests.visible_weather) {
        bits |= kVisibleWeatherSyncBit;
    }
    if (requests.visible_saying) {
        bits |= kVisibleSayingSyncBit;
    }
    if (bits != 0) {
        app_event_group_clear_bits(bits);
    }
}

} // namespace

NetworkSyncRequestSnapshot snapshot_network_sync_requests()
{
    // A loop must schedule and finish the same event snapshot even if another
    // task raises a new request while HTTPS is in progress.
    WifiPortalSaveSnapshot portal_before;
    WifiPortalSaveSnapshot portal_after;
    NetworkSyncRequestGenerationSnapshot generations_before;
    NetworkSyncRequestGenerationSnapshot generations_after;
    SettingsSyncRequestSnapshot settings_before;
    SettingsSyncRequestSnapshot settings_after;
    EventBits_t bits = 0;
    uint8_t contention_attempts = 0;
    for (;;) {
        portal_before = wifi_portal_save_snapshot_load();
        generations_before =
            network_sync_request_generation_snapshot();
        settings_before = settings_sync_request_snapshot_load();
        bits = app_event_group_get_bits();
        settings_after = settings_sync_request_snapshot_load();
        generations_after =
            network_sync_request_generation_snapshot();
        portal_after = wifi_portal_save_snapshot_load();
        const bool snapshot_consistent =
            portal_before.generation == portal_after.generation &&
            portal_before.result == portal_after.result &&
            request_generations_equal(generations_before,
                                      generations_after) &&
            settings_sync_requests_equal(settings_before,
                                         settings_after);
        if (snapshot_consistent) {
            break;
        }
        // 配网保存、设置反馈和请求代次可能同时发布。保持一致快照语义，
        // 先让出当前时间片；连续竞争时再睡眠一个 tick，避免常驻网络
        // 任务在高频发布窗口内持续参与调度。
        ++contention_attempts;
        if (contention_attempts < kNetworkSnapshotYieldAttempts) {
            taskYIELD();
        } else {
            vTaskDelay(1);
            contention_attempts = 0;
        }
    }

    NetworkSyncRequestSnapshot requests;
    requests.provisioning =
        (bits & kProvisioningSyncBit) != 0 &&
        portal_after.result == WifiPortalSaveResult::kValidating;
    if (requests.provisioning) {
        requests.provisioning_generation = portal_after.generation;
    }
    requests.manual_ntp = (bits & kManualNtpSyncBit) != 0;
    if (requests.manual_ntp) {
        requests.manual_ntp_generation =
            generations_after.manual_ntp;
        requests.manual_ntp_settings_generation =
            matching_settings_generation(
                settings_after,
                kSettingsSyncNtp,
                kManualNtpSyncBit,
                requests.manual_ntp_generation);
    }
    requests.manual_weather = (bits & kManualWeatherSyncBit) != 0;
    if (requests.manual_weather) {
        requests.manual_weather_generation =
            generations_after.manual_weather;
        requests.manual_weather_settings_generation =
            matching_settings_generation(
                settings_after,
                kSettingsSyncWeather,
                kManualWeatherSyncBit,
                requests.manual_weather_generation);
    }
    requests.manual_saying = (bits & kManualSayingSyncBit) != 0;
    if (requests.manual_saying) {
        requests.manual_saying_generation =
            generations_after.manual_saying;
        requests.manual_saying_settings_generation =
            matching_settings_generation(
                settings_after,
                kSettingsSyncSaying,
                kManualSayingSyncBit,
                requests.manual_saying_generation);
    }
    requests.visible_weather = (bits & kVisibleWeatherSyncBit) != 0;
    requests.visible_saying = (bits & kVisibleSayingSyncBit) != 0;
    requests.diagnostics = (bits & kNetworkDiagBit) != 0;
    if (requests.diagnostics) {
        requests.diagnostics_generation =
            generations_after.diagnostics;
        requests.diagnostics_settings_generation =
            matching_settings_generation(
                settings_after,
                kSettingsSyncNetworkDiag,
                kNetworkDiagBit,
                requests.diagnostics_generation);
    }
    return requests;
}

bool network_sync_request_snapshot_still_current(
    const NetworkSyncRequestSnapshot &scheduled)
{
    if (network_sync_request_bits(scheduled) == 0) {
        return true;
    }
    return scheduled.still_owned_by(snapshot_network_sync_requests());
}

EventBits_t network_sync_request_bits(const NetworkSyncRequestSnapshot &requests)
{
    EventBits_t bits = 0;
    if (requests.provisioning) {
        bits |= kProvisioningSyncBit;
    }
    if (requests.manual_ntp) {
        bits |= kManualNtpSyncBit;
    }
    if (requests.manual_weather) {
        bits |= kManualWeatherSyncBit;
    }
    if (requests.manual_saying) {
        bits |= kManualSayingSyncBit;
    }
    if (requests.visible_weather) {
        bits |= kVisibleWeatherSyncBit;
    }
    if (requests.visible_saying) {
        bits |= kVisibleSayingSyncBit;
    }
    if (requests.diagnostics) {
        bits |= kNetworkDiagBit;
    }
    return bits;
}

void clear_network_request_bits()
{
    invalidate_network_sync_requests(kNetworkRequestClearBits);
    clear_config_event_bits(kNetworkRequestClearBits,
                            kNetworkRequestResetReason);
}

void finish_settings_sync_and_clear_bit(SettingsSyncOp op,
                                        const char *status,
                                        EventBits_t bit,
                                        uint32_t request_generation,
                                        uint32_t settings_generation)
{
    finish_requested_settings_sync(true,
                                   op,
                                   status,
                                   bit,
                                   request_generation,
                                   settings_generation,
                                   true);
}

void finish_offline_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (wifi_radio_on_load() && !setup_portal_active_load()) {
        stop_wifi_radio(true);
        request_wifi_radio_stop_if_running();
    }
    clear_network_request_bits();
    if (requests.provisioning) {
        complete_provisioning_sync_request(
            requests.provisioning_generation);
    }
    finish_requested_manual_syncs(requests,
                                  ui_text(kNetworkStatusOfflineModeEnabled),
                                  false);
    if (requests.diagnostics) {
        network_diag_finish_with_status(ui_text(kNetworkStatusOfflineModeEnabled));
        finish_settings_sync(kSettingsSyncNetworkDiag,
                             requests.diagnostics_settings_generation,
                             ui_text(kNetworkStatusOfflineModeEnabled));
    }
}

void finish_unconfigured_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    finish_requested_manual_syncs(requests,
                                  ui_text(kNetworkStatusWifiNotConfigured),
                                  true);
    clear_requested_visible_syncs(requests);
    if (requests.provisioning) {
        complete_provisioning_sync_request(
            requests.provisioning_generation);
    }
    if (requests.diagnostics) {
        network_diag_finish_unavailable(
            ui_text(kNetworkDiagIpLocationWifiNotConfigured));
        finish_network_diagnostics_sync(requests);
    }
}

void finish_low_battery_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (!requests.weather_due() && !requests.saying_due() &&
        !requests.diagnostics) {
        return;
    }
    finish_requested_settings_sync(requests.manual_weather,
                                   kSettingsSyncWeather,
                                   ui_text(kNetworkSyncLowBatterySkipped),
                                   kManualWeatherSyncBit,
                                   requests.manual_weather_generation,
                                   requests.manual_weather_settings_generation,
                                   true);
    finish_requested_settings_sync(requests.manual_saying,
                                   kSettingsSyncSaying,
                                   ui_text(kNetworkSyncLowBatterySkipped),
                                   kManualSayingSyncBit,
                                   requests.manual_saying_generation,
                                   requests.manual_saying_settings_generation,
                                   true);
    clear_requested_visible_syncs(requests);
    if (requests.diagnostics) {
        network_diag_finish_with_status(ui_text(kNetworkSyncLowBatterySkipped));
        finish_settings_sync_and_clear_bit(kSettingsSyncNetworkDiag,
                                           ui_text(kNetworkSyncLowBatterySkipped),
                                           kNetworkDiagBit,
                                           requests.diagnostics_generation,
                                           requests.diagnostics_settings_generation);
    }
}

void finish_failed_sync_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (requests.provisioning) {
        complete_provisioning_sync_request(
            requests.provisioning_generation);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           ui_text(kNetworkSyncTimeFailed),
                                           kManualNtpSyncBit,
                                           requests.manual_ntp_generation,
                                           requests.manual_ntp_settings_generation);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           ui_text(kNetworkSyncWeatherFailed),
                                           kManualWeatherSyncBit,
                                           requests.manual_weather_generation,
                                           requests.manual_weather_settings_generation);
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           ui_text(kNetworkSyncSayingFailed),
                                           kManualSayingSyncBit,
                                           requests.manual_saying_generation,
                                           requests.manual_saying_settings_generation);
    }
    clear_requested_visible_syncs(requests);
}

void finish_successful_sync_requests(const NetworkSyncRequestSnapshot &requests,
                                     bool ntp_ok,
                                     bool weather_ok,
                                     bool saying_ok)
{
    if (requests.provisioning) {
        complete_provisioning_sync_request(
            requests.provisioning_generation);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           ntp_ok ? ui_text(kNetworkSyncTimeComplete)
                                                  : ui_text(kNetworkSyncTimeFailed),
                                           kManualNtpSyncBit,
                                           requests.manual_ntp_generation,
                                           requests.manual_ntp_settings_generation);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           weather_ok ? ui_text(kNetworkSyncWeatherComplete)
                                                      : ui_text(kNetworkSyncWeatherFailed),
                                           kManualWeatherSyncBit,
                                           requests.manual_weather_generation,
                                           requests.manual_weather_settings_generation);
    }
    // Manual completion already wakes the settings UI through
    // finish_settings_sync(). Automatic boot success and visible-page
    // completion still need one explicit wake because weather publication
    // only sets the ready bit.
    if (!requests.manual_weather &&
        (weather_ok || requests.visible_weather)) {
        notify_ui_task();
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           saying_ok ? ui_text(kNetworkSyncSayingComplete)
                                                      : ui_text(kNetworkSyncSayingFailed),
                                           kManualSayingSyncBit,
                                           requests.manual_saying_generation,
                                           requests.manual_saying_settings_generation);
    }
    // A successful saying publication wakes the UI in daily_saying.cpp.
    // Preserve the failure wake so a visible page can leave its pending state.
    if (requests.visible_saying && !saying_ok) {
        notify_ui_task();
    }
    clear_requested_visible_syncs(requests);
}

bool network_diagnostics_request_pending()
{
    return (app_event_group_get_bits() & kNetworkDiagBit) != 0;
}

void cancel_network_diagnostics_sync()
{
    if (!network_diagnostics_request_pending()) {
        return;
    }
    const SettingsSyncRequestSnapshot settings =
        settings_sync_request_snapshot_load();
    invalidate_network_sync_requests(kNetworkDiagBit);
    network_diag_reset();
    finish_settings_sync(kSettingsSyncNetworkDiag,
                         settings.generation,
                         ui_text(kNetworkSyncNetworkDiagCanceled));
    notify_network_sync_runtime_state_changed();
}

void finish_network_diagnostics_sync(
    const NetworkSyncRequestSnapshot &requests)
{
    finish_settings_sync_and_clear_bit(kSettingsSyncNetworkDiag,
                                       ui_text(kNetworkSyncNetworkDiagComplete),
                                       kNetworkDiagBit,
                                       requests.diagnostics_generation,
                                       requests.diagnostics_settings_generation);
}
