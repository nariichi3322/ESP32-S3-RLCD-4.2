// 执行启动页 Wi-Fi 连接、时间校准和当前主页数据的限时同步流程。
#include "network_services.h"

#include "app_text_format.h"
#include "network_sync_schedule.h"
#include "network_task_guards.h"
#include "ui_views.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr int64_t kMicrosecondsPerMillisecond = 1000;
constexpr uint32_t kBootScreenShortDelayMs = 200;
constexpr uint32_t kBootScreenOfflineDelayMs = 600;
constexpr uint32_t kBootScreenSetupDelayMs = 1500;
constexpr int kBootScreenCompletePercent = 100;
constexpr int kBootWeatherMinRemainingMs = 250;
constexpr int kBootSayingMinRemainingMs = 700;
constexpr int kBootNtpMinRemainingMs = 600;
constexpr size_t kBootSetupDetailTextSize = 64;
constexpr const char *kBootDetailStartingClock = "Starting clock";
constexpr const char *kBootDetailSynchronizingTime = "Synchronizing time";
constexpr const char *kBootSetupDetailFallback = "Setup AP: --";
constexpr const char *kBootSetupDetailFormat = "Setup AP: %s";
constexpr const char *kBootRtcInvalidNtpPriorityLog =
    "system time invalid after Wi-Fi connect, prioritizing boot NTP";

class BootSyncDeadlineGuard {
public:
    BootSyncDeadlineGuard()
    {
        g_boot_sync_deadline_us = esp_timer_get_time() +
                                  static_cast<int64_t>(kBootStartupBudgetMs) *
                                      kMicrosecondsPerMillisecond;
    }

    ~BootSyncDeadlineGuard()
    {
        g_boot_sync_deadline_us = 0;
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
        strlcpy(out, kBootSetupDetailFallback, out_len);
    }
}

void format_boot_setup_detail(char *out, size_t out_len)
{
    if (!app_text::output_buffer_available(out, out_len)) {
        return;
    }
    int written = snprintf(out, out_len, kBootSetupDetailFormat, g_ap_ssid);
    copy_boot_detail_fallback_on_format_error(written, out, out_len);
}

bool active_work_page_uses_weather_data()
{
    return g_active_work_page == kWorkPageWeatherClock ||
           g_active_work_page == kWorkPageWeatherBoard;
}

bool active_work_page_uses_daily_saying()
{
    return g_active_work_page == kWorkPageGallery;
}
} // namespace

int boot_sync_remaining_ms()
{
    return network_boot_budget_remaining_ms(g_boot_sync_deadline_us,
                                            esp_timer_get_time());
}

void run_boot_connectivity_sync()
{
    if (g_offline_mode_ui_enabled) {
        update_boot_screen(kBootScreenCompletePercent, "Offline mode", "Using RTC time");
        vTaskDelay(pdMS_TO_TICKS(kBootScreenOfflineDelayMs));
        return;
    }
    if (!g_have_wifi_creds) {
        char detail[kBootSetupDetailTextSize] = {};
        format_boot_setup_detail(detail, sizeof(detail));
        update_boot_screen(kBootScreenCompletePercent, "Setup mode", detail);
        vTaskDelay(pdMS_TO_TICKS(kBootScreenSetupDelayMs));
        return;
    }

    update_boot_screen(18, "Connecting Wi-Fi", g_wifi_ssid);
    NetworkAwakeLockGuard awake_lock;
    BootSyncDeadlineGuard deadline_guard;
    if (!start_wifi_radio(false)) {
        update_boot_screen(kBootScreenCompletePercent, "Wi-Fi start failed", kBootDetailStartingClock);
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        awake_lock.release();
        return;
    }
    int remaining_ms = boot_sync_remaining_ms();
    uint32_t wifi_timeout_ms = remaining_ms > 0 && remaining_ms < kBootWifiConnectTimeoutMs
                                   ? remaining_ms
                                   : kBootWifiConnectTimeoutMs;
    if (!wait_for_wifi_connected(wifi_timeout_ms)) {
        update_boot_screen(kBootScreenCompletePercent, "Wi-Fi timeout", "Check SSID or password");
        vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
        stop_wifi_radio();
        awake_lock.release();
        return;
    }

    bool boot_weather_page_visible = active_work_page_uses_weather_data();
    bool boot_gallery_page_visible = active_work_page_uses_daily_saying();
    update_boot_screen(42, "Wi-Fi connected", boot_weather_page_visible ? "Loading weather" : "Checking time");
    bool ntp_attempted = false;
    bool ntp_ok = false;
    if (!is_time_valid()) {
        ESP_LOGI(TAG, "%s", kBootRtcInvalidNtpPriorityLog);
        remaining_ms = boot_sync_remaining_ms();
        if (remaining_ms > kBootNtpMinRemainingMs) {
            ntp_attempted = true;
            update_boot_screen(46, kBootDetailSynchronizingTime, "Restoring lost RTC time");
            ntp_ok = perform_ntp_sync(kBootNtpRetries);
            update_boot_screen(50,
                               ntp_ok ? "Time synchronized" : "NTP retry later",
                               ntp_ok ? "Loading page data" : "Will retry in background");
        }
    }
    remaining_ms = boot_sync_remaining_ms();
    if (boot_weather_page_visible && g_have_weather_key && !g_low_battery_mode &&
        remaining_ms > kBootWeatherMinRemainingMs) {
        bool weather_ok = false;
        update_boot_screen(58, "Loading weather", "Fetching API data");
        {
            NetworkHttpTimeoutGuard timeout_guard(kHttpBootTimeoutMs);
            NetworkDisplayDmaGuard display_guard(true);
            weather_ok = perform_weather_update();
        }
        update_boot_screen(weather_ok ? 76 : 68,
                           weather_ok ? "Weather ready" : "Weather retry later",
                           weather_ok ? kBootDetailSynchronizingTime : "Will sync in background");
    } else if (boot_weather_page_visible && g_have_weather_key && !g_low_battery_mode) {
        update_boot_screen(68, "Weather retry later", kBootDetailStartingClock);
    } else if (g_low_battery_mode) {
        update_boot_screen(58, "Weather skipped", "Low battery");
    } else if (!boot_weather_page_visible) {
        update_boot_screen(58, "Weather deferred", "Open weather page");
    } else {
        update_boot_screen(58, "Weather skipped", "API Key not configured");
    }

    remaining_ms = boot_sync_remaining_ms();
    if (boot_gallery_page_visible && !g_low_battery_mode &&
        remaining_ms > kBootSayingMinRemainingMs) {
        update_boot_screen(78, "Loading quote", "Fetching daily text");
        bool saying_ok = false;
        {
            NetworkHttpTimeoutGuard timeout_guard(kHttpBootTimeoutMs);
            NetworkDisplayDmaGuard display_guard(true);
            saying_ok = perform_daily_saying_update();
        }
        update_boot_screen(saying_ok ? 80 : 78,
                           saying_ok ? "Quote ready" : "Quote retry later",
                           kBootDetailSynchronizingTime);
    } else if (!boot_gallery_page_visible && !g_low_battery_mode) {
        update_boot_screen(78, "Quote deferred", "Open image page");
    }

    remaining_ms = boot_sync_remaining_ms();
    if (!ntp_attempted && remaining_ms > kBootNtpMinRemainingMs) {
        update_boot_screen(82, kBootDetailSynchronizingTime, "Short NTP check");
        ntp_ok = perform_ntp_sync(kBootNtpRetries);
    }
    update_boot_screen(kBootScreenCompletePercent,
                       ntp_ok ? "Time synchronized" : "NTP retry later",
                       kBootDetailStartingClock);

    vTaskDelay(pdMS_TO_TICKS(kBootScreenShortDelayMs));
    stop_wifi_radio();
    awake_lock.release();
}

void boot_connectivity_task(void *)
{
    run_boot_connectivity_sync();
    xEventGroupSetBits(g_app_events, kBootSyncDoneBit);
    g_boot_sync_task_handle = nullptr;
    vTaskDelete(nullptr);
}
