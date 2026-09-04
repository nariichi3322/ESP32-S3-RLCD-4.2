// 执行启动页 Wi-Fi 连接与时间校准，页面 HTTPS 数据统一留给后台错峰同步。
#include "network_boot_sync.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "app_text_format.h"
#include "network_credentials_state.h"
#include "offline_mode_state.h"
#include "ntp_services.h"
#include "network_sync_schedule.h"
#include "network_task_guards.h"
#include "sensor_time.h"
#include "ui_boot_screen.h"
#include "ui_language.h"
#include "wifi_portal_state.h"
#include "wifi_radio_services_internal.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <stdio.h>
#include <string.h>

namespace {
static portMUX_TYPE s_boot_sync_deadline_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_boot_sync_deadline_us = 0;
constexpr int64_t kMicrosecondsPerMillisecond = 1000;
constexpr uint32_t kBootScreenShortDelayMs = 200;
constexpr uint32_t kBootScreenSetupDelayMs = 1500;
constexpr int kBootScreenCompletePercent = 100;
constexpr int kBootNtpMinRemainingMs = 600;
constexpr size_t kBootSetupDetailTextSize = 64;
struct BootSyncText {
    const char *traditional;
    const char *simplified;
    const char *english;
};

const char *boot_sync_text(const BootSyncText &text)
{
    return ui_language_text(text.traditional, text.simplified, text.english);
}

constexpr BootSyncText kBootDetailStartingClock = {
    "啟動時鐘", "启动时钟", "Starting clock"};
constexpr BootSyncText kBootDetailPowerLockUnavailable = {
    "電源鎖定不可用", "电源锁定不可用", "Power lock unavailable"};
constexpr BootSyncText kBootDetailSynchronizingTime = {
    "同步時間中", "同步时间中", "Synchronizing time"};
constexpr BootSyncText kBootDetailPageDataQueued = {
    "頁面資料已排程", "页面数据已排程", "Page data queued"};
constexpr BootSyncText kBootDetailBackgroundRefresh = {
    "啟動後重新整理中", "启动后刷新中", "Refreshing after startup"};
constexpr BootSyncText kBootSetupDetailFallback = {
    "設定 AP: --", "设置 AP: --", "Setup AP: --"};
constexpr BootSyncText kBootSetupDetailFormat = {
    "設定 AP: %s", "设置 AP: %s", "Setup AP: %s"};
constexpr BootSyncText kBootStatusOfflineMode = {
    "離線模式", "离线模式", "Offline mode"};
constexpr BootSyncText kBootStatusSettingsMode = {
    "設定模式", "设置模式", "Settings mode"};
constexpr BootSyncText kBootStatusConnectingWifi = {
    "連線 Wi-Fi", "连接 Wi-Fi", "Connecting Wi-Fi"};
constexpr BootSyncText kBootStatusWifiStartFailed = {
    "Wi-Fi 啟動失敗", "Wi-Fi 启动失败", "Wi-Fi start failed"};
constexpr BootSyncText kBootStatusWifiTimeout = {
    "Wi-Fi 連線逾時", "Wi-Fi 连接超时", "Wi-Fi timeout"};
constexpr BootSyncText kBootDetailCheckSsidOrPassword = {
    "請檢查 SSID 或密碼", "请检查 SSID 或密码", "Check SSID or password"};
constexpr BootSyncText kBootStatusWifiConnected = {
    "Wi-Fi 已連線", "Wi-Fi 已连接", "Wi-Fi connected"};
constexpr BootSyncText kBootDetailCheckingTime = {
    "檢查時間", "检查时间", "Checking time"};
constexpr BootSyncText kBootDetailRestoringRtcTime = {
    "還原遺失的 RTC 時間", "恢复丢失的 RTC 时间", "Restoring lost RTC time"};
constexpr BootSyncText kBootStatusTimeSynchronized = {
    "時間已同步", "时间已同步", "Time synchronized"};
constexpr BootSyncText kBootStatusNtpRetryLater = {
    "稍後重試 NTP", "稍后重试 NTP", "NTP retry later"};
constexpr BootSyncText kBootDetailRetryInBackground = {
    "將在背景重試", "将在后台重试", "Will retry in background"};
constexpr BootSyncText kBootDetailShortNtpCheck = {
    "快速檢查 NTP", "快速检查 NTP", "Short NTP check"};
constexpr const char *kBootRtcInvalidNtpPriorityLog =
    "system time invalid after Wi-Fi connect, prioritizing boot NTP";
constexpr const char *kBootPageDataDeferredLog =
    "boot page HTTPS deferred to staggered background sync";

static int64_t load_boot_sync_deadline_us()
{
    portENTER_CRITICAL(&s_boot_sync_deadline_mux);
    const int64_t deadline_us = s_boot_sync_deadline_us;
    portEXIT_CRITICAL(&s_boot_sync_deadline_mux);
    return deadline_us;
}

static void store_boot_sync_deadline_us(int64_t deadline_us)
{
    portENTER_CRITICAL(&s_boot_sync_deadline_mux);
    s_boot_sync_deadline_us = deadline_us;
    portEXIT_CRITICAL(&s_boot_sync_deadline_mux);
}

class BootSyncDeadlineGuard {
public:
    BootSyncDeadlineGuard()
    {
        store_boot_sync_deadline_us(
            esp_timer_get_time() +
            static_cast<int64_t>(kBootStartupBudgetMs) *
                kMicrosecondsPerMillisecond);
    }

    ~BootSyncDeadlineGuard()
    {
        store_boot_sync_deadline_us(0);
    }

    BootSyncDeadlineGuard(const BootSyncDeadlineGuard &) = delete;
    BootSyncDeadlineGuard &operator=(const BootSyncDeadlineGuard &) = delete;
};

void copy_boot_detail_fallback_on_format_error(int written, char *out, size_t out_len)
{
    if (!app_text::output_buffer_available(out, out_len)) {
        return;
    }
    if (app_text::format_failed(written, out_len)) {
        strlcpy(out, boot_sync_text(kBootSetupDetailFallback), out_len);
    }
}

void format_boot_setup_detail(char *out, size_t out_len)
{
    if (!app_text::output_buffer_available(out, out_len)) {
        return;
    }
    char setup_ap_ssid[kWifiSetupApSsidTextLen] = {};
    (void)wifi_setup_ap_ssid_snapshot(setup_ap_ssid, sizeof(setup_ap_ssid));
    int written = snprintf(out, out_len, boot_sync_text(kBootSetupDetailFormat), setup_ap_ssid);
    copy_boot_detail_fallback_on_format_error(written, out, out_len);
}

void finish_boot_network_session(NetworkAwakeLockGuard &awake_lock)
{
    stop_wifi_radio();
    awake_lock.release();
    request_wifi_radio_stop_if_running();
}

} // namespace

int boot_sync_remaining_ms()
{
    return network_boot_budget_remaining_ms(load_boot_sync_deadline_us(),
                                            esp_timer_get_time());
}

void run_boot_connectivity_sync()
{
    char wifi_ssid[kNetworkWifiSsidLen] = {};
    if (offline_mode_enabled_load()) {
        update_boot_screen(kBootScreenCompletePercent,
                           boot_sync_text(kBootStatusOfflineMode),
                           boot_sync_text(kBootDetailStartingClock));
        request_wifi_radio_stop_if_running();
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        return;
    }
    if (!network_wifi_credentials_configured() ||
        !network_wifi_ssid_snapshot(wifi_ssid, sizeof(wifi_ssid))) {
        char detail[kBootSetupDetailTextSize] = {};
        format_boot_setup_detail(detail, sizeof(detail));
        update_boot_screen(kBootScreenCompletePercent,
                           boot_sync_text(kBootStatusSettingsMode), detail);
        vTaskDelay(pdMS_TO_TICKS(kBootScreenSetupDelayMs));
        return;
    }

    update_boot_screen(18, boot_sync_text(kBootStatusConnectingWifi), wifi_ssid);
    NetworkAwakeLockGuard awake_lock;
    BootSyncDeadlineGuard deadline_guard;
    if (!awake_lock.locked()) {
        update_boot_screen(kBootScreenCompletePercent,
                           boot_sync_text(kBootDetailPowerLockUnavailable),
                           boot_sync_text(kBootDetailStartingClock));
        service_wifi_radio_stop_when_idle();
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        return;
    }
    if (!start_wifi_radio(false)) {
        update_boot_screen(kBootScreenCompletePercent,
                           boot_sync_text(kBootStatusWifiStartFailed),
                           boot_sync_text(kBootDetailStartingClock));
        awake_lock.release();
        // A running radio can fail while being reconfigured. Register a real
        // close request after releasing this session's PM lock so that rare
        // partial-start failures cannot leave Wi-Fi powered indefinitely.
        request_wifi_radio_stop_if_running();
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        return;
    }
    int remaining_ms = boot_sync_remaining_ms();
    uint32_t wifi_timeout_ms = remaining_ms > 0 && remaining_ms < kBootWifiConnectTimeoutMs
                                   ? remaining_ms
                                   : kBootWifiConnectTimeoutMs;
    if (!wait_for_wifi_connected(wifi_timeout_ms)) {
        update_boot_screen(kBootScreenCompletePercent,
                           boot_sync_text(kBootStatusWifiTimeout),
                           boot_sync_text(kBootDetailCheckSsidOrPassword));
        finish_boot_network_session(awake_lock);
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        return;
    }

    update_boot_screen(42,
                       boot_sync_text(kBootStatusWifiConnected),
                       boot_sync_text(kBootDetailCheckingTime));
    ESP_LOGI(TAG, "%s", kBootPageDataDeferredLog);
    bool ntp_attempted = false;
    bool ntp_ok = false;
    if (!is_system_time_plausible()) {
        ESP_LOGI(TAG, "%s", kBootRtcInvalidNtpPriorityLog);
        remaining_ms = boot_sync_remaining_ms();
        if (remaining_ms > kBootNtpMinRemainingMs) {
            ntp_attempted = true;
            update_boot_screen(46,
                               boot_sync_text(kBootDetailSynchronizingTime),
                               boot_sync_text(kBootDetailRestoringRtcTime));
            ntp_ok = perform_ntp_sync(kBootNtpRetries);
            update_boot_screen(72,
                               ntp_ok ? boot_sync_text(kBootStatusTimeSynchronized) :
                                        boot_sync_text(kBootStatusNtpRetryLater),
                               ntp_ok ? boot_sync_text(kBootDetailPageDataQueued) :
                                        boot_sync_text(kBootDetailRetryInBackground));
        }
    }
    remaining_ms = boot_sync_remaining_ms();
    if (!ntp_attempted && remaining_ms > kBootNtpMinRemainingMs) {
        update_boot_screen(82,
                           boot_sync_text(kBootDetailSynchronizingTime),
                           boot_sync_text(kBootDetailShortNtpCheck));
        ntp_ok = perform_ntp_sync(kBootNtpRetries);
    }
    update_boot_screen(kBootScreenCompletePercent,
                       ntp_ok ? boot_sync_text(kBootStatusTimeSynchronized) :
                                boot_sync_text(kBootStatusNtpRetryLater),
                       boot_sync_text(kBootDetailBackgroundRefresh));

    finish_boot_network_session(awake_lock);
    vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
}

void boot_connectivity_task(void *)
{
    run_boot_connectivity_sync();
    app_event_group_set_bits(kBootSyncDoneBit);
    vTaskDelete(nullptr);
}
