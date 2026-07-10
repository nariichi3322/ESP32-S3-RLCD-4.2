// 处理设置页网络、声音、显示和系统操作，不承担页面绘制。
#include "ui_views.h"

#include "audio_services.h"
#include "network_services.h"
#include "ota_services.h"
#include "ui_text_format.h"

#include <cstdarg>

namespace {
constexpr int kChimeVolumeLevels[] = {20, 40, 60, 80, 100};

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr int kChimeVolumeLevelCount = static_cast<int>(array_count(kChimeVolumeLevels));
constexpr int kDefaultChimeVolumePercent = kChimeVolumeLevels[0];
constexpr uint8_t kAllWorkPageMask = static_cast<uint8_t>((1U << kWorkPageCount) - 1);
constexpr int kSettingsFeedbackDefaultMs = 2500;
constexpr int kSettingsFeedbackBusyMs = 2000;
constexpr int kSettingsFeedbackSavedMs = 1800;
constexpr int kSettingsFeedbackInstructionMs = 3500;
constexpr const char *kSettingsSaveFailedFeedback = "保存失败";
constexpr const char *kSettingsOrderSavedFeedback = "页面顺序已保存";
constexpr const char *kSettingsSyncBusyFeedback = "请等待同步完成";
constexpr const char *kSettingsOfflineEnabledFeedback = "离线模式已开启";
constexpr const char *kSettingsOfflineDisabledFeedback = "离线模式已关闭";
constexpr const char *kManualWeatherCityEditFeedback = "请进入配网页修改";
constexpr const char *kManualWeatherCityClearConfirmFeedback = "再次确认清除";
constexpr const char *kManualWeatherCityAutoFeedback = "已恢复自动定位";
constexpr const char *kManualNtpSyncFeedback = "正在同步时间...";
constexpr const char *kManualWeatherSyncFeedback = "正在同步天气...";
constexpr const char *kManualSayingSyncFeedback = "正在更新一言...";
constexpr const char *kSoundVolumeFeedbackFormat = "音量 %d%%";
constexpr const char *kSoundIndexFeedbackFormat = "声音 %d";
constexpr const char *kHourlyChimeEnabledFeedback = "整点提醒已开启";
constexpr const char *kHourlyChimeDisabledFeedback = "整点提醒已关闭";
constexpr const char *kAllDayChimeEnabledFeedback = "全天提醒已开启";
constexpr const char *kAllDayChimeDisabledFeedback = "全天提醒已关闭";
constexpr const char *kPageOrderInstructionFeedback = "BOOT交换并保存";
constexpr const char *kPageSwitchInstructionFeedback = "页面开关：BOOT切换";
constexpr const char *kLastWorkPageFeedback = "至少保留一个页面";
constexpr const char *kWorkPageFeedbackFormat = "%s%s";
constexpr const char *kWorkPageEnabledSuffix = "已开启";
constexpr const char *kWorkPageDisabledSuffix = "已关闭";
constexpr const char *kOfflineSetupConfirmFeedback = "再次确认进入配网";
constexpr const char *kSetupStartFailedFeedback = "配网启动失败";
constexpr const char *kOfflineSetupInstructionFeedback = "请完成配网后关闭";
constexpr const char *kNetworkDiagSyncFeedback = "正在网络检测...";
constexpr const char *kFactoryResetConfirmFeedback = "再次按 BOOT 确认";
constexpr const char *kFactoryResetFailedFeedback = "恢复失败";
constexpr size_t kSettingsFeedbackTextSize = 32;
#define HOURLY_CHIME_SETTING_LOG_FORMAT "hourly chime %s"
#define ALL_DAY_CHIME_SETTING_LOG_FORMAT "hourly chime all-day %s"
#define CHIME_SETTING_ENABLED_LOG_VALUE "enabled"
#define CHIME_SETTING_DISABLED_LOG_VALUE "disabled"
#define MANUAL_WEATHER_CITY_CLEARED_SYNC_LOG "manual weather city cleared, requesting weather sync"
#define MANUAL_NTP_SYNC_REQUESTED_LOG "manual ntp sync requested"
#define MANUAL_WEATHER_SYNC_REQUESTED_LOG "manual weather sync requested"
#define MANUAL_SAYING_SYNC_REQUESTED_LOG "manual daily saying sync requested"
#define MANUAL_NETWORK_DIAG_REQUESTED_LOG "manual network diagnostics requested"
#define FACTORY_RESET_CONFIRM_REQUESTED_LOG "factory reset confirmation requested"
#define FACTORY_RESET_REQUESTED_LOG "factory reset requested from settings"
#define SYSTEM_INFO_REQUESTED_LOG "system info requested from settings"

constexpr const char *kSettingsActionTexts[] = {
    kSettingsSaveFailedFeedback,
    kSettingsOrderSavedFeedback,
    kSettingsSyncBusyFeedback,
    kSettingsOfflineEnabledFeedback,
    kSettingsOfflineDisabledFeedback,
    kManualWeatherCityEditFeedback,
    kManualWeatherCityClearConfirmFeedback,
    kManualWeatherCityAutoFeedback,
    kManualNtpSyncFeedback,
    kManualWeatherSyncFeedback,
    kManualSayingSyncFeedback,
    kSoundVolumeFeedbackFormat,
    kSoundIndexFeedbackFormat,
    kHourlyChimeEnabledFeedback,
    kHourlyChimeDisabledFeedback,
    kAllDayChimeEnabledFeedback,
    kAllDayChimeDisabledFeedback,
    kPageOrderInstructionFeedback,
    kPageSwitchInstructionFeedback,
    kLastWorkPageFeedback,
    kWorkPageFeedbackFormat,
    kWorkPageEnabledSuffix,
    kWorkPageDisabledSuffix,
    kOfflineSetupConfirmFeedback,
    kSetupStartFailedFeedback,
    kOfflineSetupInstructionFeedback,
    kNetworkDiagSyncFeedback,
    kFactoryResetConfirmFeedback,
    kFactoryResetFailedFeedback,
    HOURLY_CHIME_SETTING_LOG_FORMAT,
    ALL_DAY_CHIME_SETTING_LOG_FORMAT,
    CHIME_SETTING_ENABLED_LOG_VALUE,
    CHIME_SETTING_DISABLED_LOG_VALUE,
    MANUAL_WEATHER_CITY_CLEARED_SYNC_LOG,
    MANUAL_NTP_SYNC_REQUESTED_LOG,
    MANUAL_WEATHER_SYNC_REQUESTED_LOG,
    MANUAL_SAYING_SYNC_REQUESTED_LOG,
    MANUAL_NETWORK_DIAG_REQUESTED_LOG,
    FACTORY_RESET_CONFIRM_REQUESTED_LOG,
    FACTORY_RESET_REQUESTED_LOG,
    SYSTEM_INFO_REQUESTED_LOG,
};

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

constexpr bool work_page_index_valid(int page)
{
    return page >= 0 && page < kWorkPageCount;
}

constexpr bool chime_volume_levels_ordered_and_bounded()
{
    int previous = 0;
    for (int volume : kChimeVolumeLevels) {
        if (volume <= 0 || volume > 100 || volume <= previous) {
            return false;
        }
        previous = volume;
    }
    return true;
}

uint8_t toggled_work_page_mask(uint8_t current_mask, int page)
{
    if (!work_page_index_valid(page)) {
        return static_cast<uint8_t>(current_mask & kAllWorkPageMask);
    }
    uint8_t page_mask = static_cast<uint8_t>(1U << page);
    return static_cast<uint8_t>((current_mask ^ page_mask) & kAllWorkPageMask);
}

void set_formatted_settings_feedback(const char *format, ...)
{
    if (!format) {
        set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
        return;
    }
    char feedback[kSettingsFeedbackTextSize] = {};
    va_list args;
    va_start(args, format);
    int written = vsnprintf(feedback, sizeof(feedback), format, args);
    va_end(args);
    if (ui_text::format_failed(written, sizeof(feedback))) {
        set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
        return;
    }
    set_settings_feedback(feedback, kSettingsFeedbackDefaultMs);
}

void clear_inactive_settings_confirmation(int primary, int selected)
{
    if (!(primary == kSettingsPrimarySystem && selected == kSystemSettingsFactoryResetItem)) {
        g_factory_reset_confirm_pending = false;
    }
    if (!(primary == kSettingsPrimarySystem && selected == kSystemSettingsOfflineItem)) {
        g_offline_disable_confirm_pending = false;
    }
    if (!(primary == kSettingsPrimaryNetwork && selected == kNetworkSettingsWeatherCityItem)) {
        g_weather_city_clear_confirm_pending = false;
    }
}

static_assert(kWorkPageCount > 0 && kWorkPageCount <= 8,
              "work page mask in settings UI is stored as uint8_t");
static_assert(kAllWorkPageMask != 0, "settings UI must have at least one work page bit");
static_assert(array_count(kChimeVolumeLevels) > 0, "chime volume level list must not be empty");
static_assert(chime_volume_levels_ordered_and_bounded(),
              "chime volume levels must be ordered percentages in 1..100");
static_assert(array_count(kSettingsActionTexts) > 0, "settings action text registry must not be empty");
static_assert(cstr_array_nonempty(kSettingsActionTexts), "settings action texts must be non-empty");
} // namespace

void handle_settings_action()
{
    int primary = g_settings_primary_selection;
    if (primary < 0 || primary >= kSettingsPrimaryCount) {
        primary = kSettingsPrimaryNetwork;
    }
    int selected = g_settings_selection;
    int secondary_count = settings_secondary_count(primary);
    if (selected < 0 || selected >= secondary_count) {
        selected = 0;
    }
    g_settings_primary_selection = primary;
    g_settings_selection = selected;
    g_settings_last_activity_tick = xTaskGetTickCount();
    if (g_settings_page_order_mode) {
        normalize_work_page_order();
        int current = valid_enabled_work_page_order_index(g_settings_page_order_selection);
        int next = next_enabled_work_page_order_index(current);
        uint8_t tmp = g_work_page_order[current];
        g_work_page_order[current] = g_work_page_order[next];
        g_work_page_order[next] = tmp;
        g_settings_page_order_selection = next;
        if (save_work_page_order()) {
            g_active_work_page = first_enabled_work_page();
            set_settings_feedback(kSettingsOrderSavedFeedback, kSettingsFeedbackSavedMs);
        } else {
            set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
        }
        return;
    }
    if (!g_settings_focus_secondary) {
        g_settings_focus_secondary = true;
        g_settings_selection = 0;
        reset_settings_confirmation();
        g_settings_feedback[0] = '\0';
        return;
    }
    if (is_settings_sync_busy()) {
        set_settings_feedback(kSettingsSyncBusyFeedback, kSettingsFeedbackBusyMs);
        return;
    }
    clear_inactive_settings_confirmation(primary, selected);
    if (primary == kSettingsPrimaryNetwork) {
        if (selected == kNetworkSettingsWeatherCityItem) {
            if (!g_has_manual_weather_city) {
                set_settings_feedback(kManualWeatherCityEditFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!g_weather_city_clear_confirm_pending) {
                g_weather_city_clear_confirm_pending = true;
                set_settings_feedback(kManualWeatherCityClearConfirmFeedback, kSettingsTimeoutMs);
                return;
            }
            if (!clear_manual_weather_city()) {
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_weather_city_clear_confirm_pending = false;
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kManualWeatherCityAutoFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            begin_settings_sync(kSettingsSyncWeather, kManualWeatherSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_WEATHER_CITY_CLEARED_SYNC_LOG);
            xEventGroupSetBits(g_app_events, kManualWeatherSyncBit);
            return;
        }
        if (g_offline_mode_ui_enabled) {
            set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
            return;
        }
        if (selected == kNetworkSettingsNtpItem) {
            begin_settings_sync(kSettingsSyncNtp, kManualNtpSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_NTP_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualNtpSyncBit);
        } else if (selected == kNetworkSettingsWeatherItem) {
            begin_settings_sync(kSettingsSyncWeather, kManualWeatherSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_WEATHER_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualWeatherSyncBit);
        } else if (selected == kNetworkSettingsSayingItem) {
            begin_settings_sync(kSettingsSyncSaying, kManualSayingSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_SAYING_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualSayingSyncBit);
        }
        return;
    }
    if (primary == kSettingsPrimarySound) {
        if (selected == kSoundSettingsVolumeItem) {
            int previous = g_chime_volume_percent;
            int next = kDefaultChimeVolumePercent;
            for (int i = 0; i < kChimeVolumeLevelCount; ++i) {
                if (g_chime_volume_percent == kChimeVolumeLevels[i]) {
                    next = kChimeVolumeLevels[(i + 1) % kChimeVolumeLevelCount];
                    break;
                }
            }
            g_chime_volume_percent = next;
            if (!save_hourly_chime_setting()) {
                g_chime_volume_percent = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_formatted_settings_feedback(kSoundVolumeFeedbackFormat, g_chime_volume_percent);
            request_settings_confirmation_chime();
        } else if (selected == kSoundSettingsSoundItem) {
            int previous = g_chime_sound_index;
            g_chime_sound_index = (g_chime_sound_index + 1) % kChimeSoundCount;
            if (!save_hourly_chime_setting()) {
                g_chime_sound_index = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_formatted_settings_feedback(kSoundIndexFeedbackFormat, g_chime_sound_index + 1);
            request_settings_confirmation_chime();
        } else if (selected == kSoundSettingsHourlyItem) {
            bool previous = g_hourly_chime_enabled;
            g_hourly_chime_enabled = !g_hourly_chime_enabled;
            if (!save_hourly_chime_setting()) {
                g_hourly_chime_enabled = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_settings_feedback(g_hourly_chime_enabled ? kHourlyChimeEnabledFeedback : kHourlyChimeDisabledFeedback,
                                  kSettingsFeedbackDefaultMs);
            ESP_LOGI(TAG,
                     HOURLY_CHIME_SETTING_LOG_FORMAT,
                     g_hourly_chime_enabled ? CHIME_SETTING_ENABLED_LOG_VALUE : CHIME_SETTING_DISABLED_LOG_VALUE);
            if (g_hourly_chime_enabled) {
                request_settings_confirmation_chime();
            }
        } else if (selected == kSoundSettingsAllDayItem) {
            bool previous = g_hourly_chime_all_day;
            g_hourly_chime_all_day = !g_hourly_chime_all_day;
            if (!save_hourly_chime_setting()) {
                g_hourly_chime_all_day = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_settings_feedback(g_hourly_chime_all_day ? kAllDayChimeEnabledFeedback : kAllDayChimeDisabledFeedback,
                                  kSettingsFeedbackDefaultMs);
            ESP_LOGI(TAG,
                     ALL_DAY_CHIME_SETTING_LOG_FORMAT,
                     g_hourly_chime_all_day ? CHIME_SETTING_ENABLED_LOG_VALUE : CHIME_SETTING_DISABLED_LOG_VALUE);
            if (g_hourly_chime_all_day) {
                request_settings_confirmation_chime();
            }
        }
        return;
    }
    if (primary == kSettingsPrimaryDisplay) {
        if (g_settings_page_toggle_mode) {
            int page = g_settings_selection;
            if (!work_page_index_valid(page)) {
                page = kWorkPageWeatherClock;
            }
            uint8_t next_mask = toggled_work_page_mask(g_work_page_enabled_mask, page);
            if (next_mask == 0) {
                set_settings_feedback(kLastWorkPageFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            uint8_t previous = g_work_page_enabled_mask;
            g_work_page_enabled_mask = next_mask;
            if (!save_work_page_settings()) {
                g_work_page_enabled_mask = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            ensure_active_work_page_enabled();
            set_formatted_settings_feedback(kWorkPageFeedbackFormat,
                                            work_page_name(page),
                                            is_work_page_enabled(page) ? kWorkPageEnabledSuffix : kWorkPageDisabledSuffix);
            return;
        }
        if (selected == kDisplaySettingsPageSwitchItem) {
            g_settings_page_order_mode = false;
            g_settings_page_toggle_mode = true;
            g_settings_selection = 0;
            set_settings_feedback(kPageSwitchInstructionFeedback, kSettingsFeedbackInstructionMs);
            return;
        }
        if (selected == kDisplaySettingsOrderItem) {
            g_settings_page_toggle_mode = false;
            g_settings_page_order_mode = true;
            normalize_work_page_order();
            g_settings_page_order_selection = first_enabled_work_page_order_index();
            set_settings_feedback(kPageOrderInstructionFeedback, kSettingsFeedbackInstructionMs);
            return;
        }
        set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
        return;
    }
    if (primary == kSettingsPrimarySystem) {
        if (selected == kSystemSettingsOfflineItem) {
            if (!g_offline_mode_ui_enabled) {
                if (!set_offline_mode_enabled(true)) {
                    set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                    return;
                }
                g_offline_disable_confirm_pending = false;
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (can_leave_offline_mode_without_setup()) {
                if (!set_offline_mode_enabled(false)) {
                    set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                    return;
                }
                g_offline_disable_confirm_pending = false;
                set_settings_feedback(kSettingsOfflineDisabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!g_offline_disable_confirm_pending) {
                g_offline_disable_confirm_pending = true;
                set_settings_feedback(kOfflineSetupConfirmFeedback, kSettingsTimeoutMs);
                return;
            }
            if (!start_wifi_radio(true)) {
                set_settings_feedback(kSetupStartFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_offline_disable_confirm_pending = false;
            set_settings_feedback(kOfflineSetupInstructionFeedback, kSettingsFeedbackInstructionMs);
        } else if (selected == kSystemSettingsNetworkDiagItem) {
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            begin_settings_sync(kSettingsSyncNetworkDiag, kNetworkDiagSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_NETWORK_DIAG_REQUESTED_LOG);
            network_diag_reset();
            g_settings_requested = false;
            g_network_diag_page_requested = true;
            g_settings_focus_secondary = true;
            g_settings_primary_selection = kSettingsPrimarySystem;
            g_settings_selection = 0;
            g_info_page_until_tick = 0;
            xEventGroupSetBits(g_app_events, kNetworkDiagBit);
        } else if (selected == kSystemSettingsFactoryResetItem) {
            if (!g_factory_reset_confirm_pending) {
                g_factory_reset_confirm_pending = true;
                set_settings_feedback(kFactoryResetConfirmFeedback, kSettingsTimeoutMs);
                ESP_LOGW(TAG, "%s", FACTORY_RESET_CONFIRM_REQUESTED_LOG);
                return;
            }
            ESP_LOGW(TAG, "%s", FACTORY_RESET_REQUESTED_LOG);
            if (!clear_saved_config()) {
                set_settings_feedback(kFactoryResetFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!start_wifi_radio(true)) {
                set_settings_feedback(kSetupStartFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_settings_requested = false;
            g_settings_page_toggle_mode = false;
            g_settings_page_order_mode = false;
            g_factory_reset_confirm_pending = false;
            g_offline_disable_confirm_pending = false;
        } else if (selected == kSystemSettingsInfoItem) {
            g_settings_requested = false;
            g_settings_page_toggle_mode = false;
            g_settings_page_order_mode = false;
            g_factory_reset_confirm_pending = false;
            g_boot_info_requested = true;
            g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kSettingsTimeoutMs);
            ESP_LOGI(TAG, "%s", SYSTEM_INFO_REQUESTED_LOG);
        } else if (selected == kSystemSettingsOtaItem) {
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            ota_handle_info_key();
        }
    }
}
