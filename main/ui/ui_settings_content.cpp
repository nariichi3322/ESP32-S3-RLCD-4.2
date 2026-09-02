// 生成网络、声音、显示和系统设置的二级菜单动态文案。
#include "ui_settings_content.h"

#include "app_metadata.h"
#include "custom_assets.h"
#include "manual_weather_city_state.h"
#include "offline_mode_state.h"
#include "ui_settings_confirmation_state.h"
#include "ui_gallery_rotation_state.h"
#include "ui_language.h"
#include "ui_text_format.h"
#include "weather_city_contract.h"

#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>

namespace {
#define SETTINGS_SECONDARY_FORMAT_FAILED_FORMAT "settings secondary text format failed index=%d"
#define SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT "settings secondary text index out of range: %d"

static_assert(kSettingsSecondaryTextSize > 1,
              "settings secondary text buffer must fit text and NUL");
} // namespace

void set_secondary_text(char items[][kSettingsSecondaryTextSize],
                        int index,
                        const char *text)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    ui_text::copy(items[index], kSettingsSecondaryTextSize, text);
}

void format_secondary_text(char items[][kSettingsSecondaryTextSize],
                           int index,
                           const char *format,
                           ...)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    items[index][0] = '\0';
    va_list args;
    va_start(args, format);
    int written = vsnprintf(items[index],
                            kSettingsSecondaryTextSize,
                            format ? format : "",
                            args);
    va_end(args);
    if (ui_text::format_failed(written, kSettingsSecondaryTextSize)) {
        items[index][0] = '\0';
        ESP_LOGW(TAG, SETTINGS_SECONDARY_FORMAT_FAILED_FORMAT, index);
    }
}

bool settings_secondary_index_valid(int index)
{
    return index >= 0 && index < kSettingsSecondaryMaxCount;
}

void populate_settings_secondary_items(
    int primary,
    const SettingsSecondaryStateSnapshot &state,
    char secondary_items[][kSettingsSecondaryTextSize])
{
    if (primary == kSettingsPrimaryNetwork) {
        set_secondary_text(secondary_items, kNetworkSettingsNtpItem,
                           ui_language_text("同步時間", "同步时间"));
        set_secondary_text(secondary_items, kNetworkSettingsWeatherItem,
                           ui_language_text("同步天氣", "同步天气"));
        set_secondary_text(secondary_items, kNetworkSettingsSayingItem,
                           ui_language_text("更新一言", "更新一言"));
        char city[kManualWeatherCityLen] = {};
        if (manual_weather_city_snapshot(city, sizeof(city))) {
            format_secondary_text(secondary_items,
                                  kNetworkSettingsWeatherCityItem,
                                  ui_language_text("天氣城市 %s", "天气城市 %s"),
                                  city);
        } else {
            set_secondary_text(secondary_items,
                               kNetworkSettingsWeatherCityItem,
                               ui_language_text("天氣城市 自動", "天气城市 自动"));
        }
    } else if (primary == kSettingsPrimarySound) {
        if (state.volume_percent == 0) {
            set_secondary_text(secondary_items,
                               kSoundSettingsVolumeItem,
                               ui_language_text("音量 靜音", "音量 静音"));
        } else {
            format_secondary_text(secondary_items,
                                  kSoundSettingsVolumeItem,
                                  ui_language_text("音量 %d%%", "音量 %d%%"),
                                  static_cast<int>(state.volume_percent));
        }
        format_secondary_text(secondary_items,
                              kSoundSettingsSoundItem,
                              ui_language_text("聲音選擇 %d", "声音选择 %d"),
                              static_cast<int>(state.sound_index) + 1);
        set_secondary_text(secondary_items, kSoundSettingsHourlyItem,
                           ui_language_text("整點提醒 7:00 - 22:00", "整点提醒 7:00 - 22:00"));
        set_secondary_text(secondary_items, kSoundSettingsAllDayItem,
                           ui_language_text("全天提醒 0:00 - 24:00", "全天提醒 0:00 - 24:00"));
    } else if (primary == kSettingsPrimaryDisplay) {
        set_secondary_text(secondary_items,
                           kDisplaySettingsPageSwitchItem,
                           ui_language_text("頁面開關", "页面开关"));
        set_secondary_text(secondary_items,
                           kDisplaySettingsOrderItem,
                           ui_language_text("頁面順序", "页面顺序"));
        if (state.alarm_enabled) {
            format_secondary_text(secondary_items,
                                  kDisplaySettingsAlarmItem,
                                  ui_language_text("鬧鐘 %02d:%02d", "闹钟 %02d:%02d"),
                                  state.alarm_hour,
                                  state.alarm_minute);
        } else {
            set_secondary_text(secondary_items,
                               kDisplaySettingsAlarmItem,
                               ui_language_text("鬧鐘 --:--", "闹钟 --:--"));
        }
        set_secondary_text(secondary_items,
                           kDisplaySettingsXiaozhiAutoReturnItem,
                           ui_language_text("自動返回", "自动返回"));
        format_secondary_text(
            secondary_items,
            kDisplaySettingsGalleryRotationItem,
            ui_language_text("自訂圖 %s", "自定义图 %s"),
            effective_gallery_rotation_label(gallery_rotation_period_load(),
                                             custom_assets_gallery_count()));
    } else {
        format_secondary_text(secondary_items,
                              kSystemSettingsOfflineItem,
                              ui_language_text("離線模式 %s", "离线模式 %s"),
                              offline_mode_enabled_load()
                                  ? ui_language_text("開", "开")
                                  : ui_language_text("關", "关"));
        set_secondary_text(secondary_items,
                           kSystemSettingsFactoryResetItem,
                           settings_confirmation_pending(SettingsConfirmation::kFactoryReset)
                               ? ui_language_text("確認恢復", "确认恢复")
                               : ui_language_text("恢復原廠", "恢复出厂"));
        set_secondary_text(secondary_items,
                           kSystemSettingsInfoItem,
                           ui_language_text("關於本機", "关于本机"));
        set_secondary_text(secondary_items,
                           kSystemSettingsClearCodexBondsItem,
                           ui_language_text("清除配對", "清除配对"));
        set_secondary_text(secondary_items,
                           kSystemSettingsLanguageItem,
                           ui_language_text("語言 繁", "语言 简"));
        set_secondary_text(secondary_items,
                           kSystemSettingsSetupItem,
                           ui_language_text("設定模式", "设置模式"));
        set_secondary_text(secondary_items,
                           kSystemSettingsOtaItem,
                           ui_language_text("檢查更新", "检查更新"));
        set_secondary_text(secondary_items,
                           kSystemSettingsNetworkDiagItem,
                           ui_language_text("網路檢測", "网络检测"));
    }
}
