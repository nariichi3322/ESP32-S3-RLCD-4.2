// 生成网络、声音、显示和系统设置的二级菜单动态文案。
#include "ui_settings_content.h"

#include "app_metadata.h"
#include "custom_assets.h"
#include "manual_weather_city_state.h"
#include "offline_mode_state.h"
#include "ui_settings_confirmation_state.h"
#include "ui_gallery_rotation_state.h"
#include "ui_i18n.h"
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

const char *settings_display_text(UiTextId id)
{
    // These entries are shown in the 111 px settings grid. Keep the catalog's
    // full translations for general messages, and use compact labels only at
    // this narrow menu surface.
    switch (id) {
    case UiTextId::SettingsCheckUpdates:
        return ui_text(UiTextId::SettingsCheckUpdatesCompact);
    case UiTextId::SettingsNetworkDiagnostics:
        return ui_text(UiTextId::SettingsNetworkDiagnosticsCompact);
    case UiTextId::SettingsSyncTime:
        return ui_text(UiTextId::SettingsSyncTimeCompact);
    case UiTextId::SettingsSyncWeather:
        return ui_text(UiTextId::SettingsSyncWeatherCompact);
    case UiTextId::SettingsUpdateSaying:
        return ui_text(UiTextId::SettingsUpdateSayingCompact);
    case UiTextId::SettingsWeatherCity:
        return ui_text(UiTextId::SettingsWeatherCityCompact);
    case UiTextId::SettingsPageOrder:
        return ui_text(UiTextId::SettingsPageOrderCompact);
    case UiTextId::SettingsClearPairing:
        return ui_text(UiTextId::SettingsClearPairingCompact);
    case UiTextId::SettingsSetup:
        return ui_text(UiTextId::SettingsSetupCompact);
    case UiTextId::SettingsFactoryReset:
        return ui_text(UiTextId::SettingsFactoryResetCompact);
    case UiTextId::SettingsConfirmReset:
        return ui_text(UiTextId::SettingsConfirmResetCompact);
    case UiTextId::SettingsPageMode:
        return ui_text(UiTextId::SettingsPageModeCompact);
    case UiTextId::SettingsAutoReturn:
        return ui_text(UiTextId::SettingsAutoReturnCompact);
    default:
        return ui_text(id);
    }
}

const char *settings_display_format(UiTextId id)
{
    switch (id) {
    case UiTextId::SettingsLanguageFormat:
        return ui_format(UiTextId::SettingsLanguageCompactFormat);
    case UiTextId::SettingsWeatherCityFormat:
        return ui_format(UiTextId::SettingsWeatherCityCompactFormat);
    case UiTextId::SettingsWeatherCityAuto:
        return ui_text(UiTextId::SettingsWeatherCityAutoCompact);
    case UiTextId::SettingsHourlyReminder:
        return ui_text(UiTextId::SettingsHourlyReminderCompact);
    case UiTextId::SettingsAllDayReminder:
        return ui_text(UiTextId::SettingsAllDayReminderCompact);
    case UiTextId::SettingsAlarmFormat:
        return ui_format(UiTextId::SettingsAlarmCompactFormat);
    case UiTextId::SettingsAlarmPlaceholder:
        return ui_text(UiTextId::SettingsAlarmCompactPlaceholder);
    case UiTextId::SettingsCustomImageFormat:
        return ui_format(UiTextId::SettingsCustomImageCompactFormat);
    case UiTextId::SettingsOfflineFormat:
        return ui_format(UiTextId::SettingsOfflineCompactFormat);
    default:
        return ui_format(id);
    }
}
} // namespace

void set_secondary_text(char items[][kSettingsSecondaryTextSize],
                        int index,
                        const char *text)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    ui_text_format::copy(items[index], kSettingsSecondaryTextSize, text);
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
    if (ui_text_format::format_failed(written, kSettingsSecondaryTextSize)) {
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
                           settings_display_text(UiTextId::SettingsSyncTime));
        set_secondary_text(secondary_items, kNetworkSettingsWeatherItem,
                           settings_display_text(UiTextId::SettingsSyncWeather));
        set_secondary_text(secondary_items, kNetworkSettingsSayingItem,
                           settings_display_text(UiTextId::SettingsUpdateSaying));
        char city[kManualWeatherCityLen] = {};
        if (manual_weather_city_snapshot(city, sizeof(city))) {
            format_secondary_text(secondary_items,
                                  kNetworkSettingsWeatherCityItem,
                                  settings_display_format(UiTextId::SettingsWeatherCityFormat),
                                  city);
        } else {
            set_secondary_text(secondary_items,
                               kNetworkSettingsWeatherCityItem,
                                 settings_display_format(UiTextId::SettingsWeatherCityAuto));
        }
    } else if (primary == kSettingsPrimarySound) {
        if (state.volume_percent == 0) {
            set_secondary_text(secondary_items,
                               kSoundSettingsVolumeItem,
                                settings_display_text(UiTextId::SettingsMuted));
        } else {
            format_secondary_text(secondary_items,
                                  kSoundSettingsVolumeItem,
                                  settings_display_format(UiTextId::SettingsVolumeFormat),
                                  static_cast<int>(state.volume_percent));
        }
        format_secondary_text(secondary_items,
                              kSoundSettingsSoundItem,
                              settings_display_format(UiTextId::SettingsSoundFormat),
                              static_cast<int>(state.sound_index) + 1);
        set_secondary_text(secondary_items, kSoundSettingsHourlyItem,
                           settings_display_format(UiTextId::SettingsHourlyReminder));
        set_secondary_text(secondary_items, kSoundSettingsAllDayItem,
                           settings_display_format(UiTextId::SettingsAllDayReminder));
    } else if (primary == kSettingsPrimaryDisplay) {
        set_secondary_text(secondary_items,
                           kDisplaySettingsPageSwitchItem,
                            settings_display_text(UiTextId::SettingsPageMode));
        set_secondary_text(secondary_items,
                           kDisplaySettingsOrderItem,
                           settings_display_text(UiTextId::SettingsPageOrder));
        if (state.alarm_enabled) {
            format_secondary_text(secondary_items,
                                  kDisplaySettingsAlarmItem,
                                   settings_display_format(UiTextId::SettingsAlarmFormat),
                                  state.alarm_hour,
                                  state.alarm_minute);
        } else {
            set_secondary_text(secondary_items,
                               kDisplaySettingsAlarmItem,
                                settings_display_format(UiTextId::SettingsAlarmPlaceholder));
        }
        set_secondary_text(secondary_items,
                           kDisplaySettingsXiaozhiAutoReturnItem,
                            settings_display_text(UiTextId::SettingsAutoReturn));
        format_secondary_text(
            secondary_items,
            kDisplaySettingsGalleryRotationItem,
             settings_display_format(UiTextId::SettingsCustomImageFormat),
            effective_gallery_rotation_label(gallery_rotation_period_load(),
                                             custom_assets_gallery_count()));
    } else {
        format_secondary_text(secondary_items,
                              kSystemSettingsOfflineItem,
                               settings_display_format(UiTextId::SettingsOfflineFormat),
                               offline_mode_enabled_load()
                                   ? settings_display_text(UiTextId::SettingsOn)
                                   : settings_display_text(UiTextId::SettingsOff));
        set_secondary_text(secondary_items,
                           kSystemSettingsFactoryResetItem,
                           settings_confirmation_pending(SettingsConfirmation::kFactoryReset)
                                 ? settings_display_text(UiTextId::SettingsConfirmReset)
                                 : settings_display_text(UiTextId::SettingsFactoryReset));
        set_secondary_text(secondary_items,
                           kSystemSettingsInfoItem,
                             settings_display_text(UiTextId::SettingsAbout));
        set_secondary_text(secondary_items,
                           kSystemSettingsClearCodexBondsItem,
                             settings_display_text(UiTextId::SettingsClearPairing));
        UiTextId language_name = UiTextId::LanguageTraditionalCompact;
        switch (ui_language_target()) {
        case UiLanguage::Simplified: language_name = UiTextId::LanguageSimplifiedCompact; break;
        case UiLanguage::English: language_name = UiTextId::LanguageEnglishCompact; break;
        case UiLanguage::Japanese: language_name = UiTextId::LanguageJapaneseCompact; break;
        default: break;
        }
        format_secondary_text(secondary_items,
                              kSystemSettingsLanguageItem,
                              settings_display_format(UiTextId::SettingsLanguageFormat),
                              ui_text(language_name));
        set_secondary_text(secondary_items,
                           kSystemSettingsSetupItem,
                           settings_display_text(UiTextId::SettingsSetup));
        set_secondary_text(secondary_items,
                           kSystemSettingsOtaItem,
                           settings_display_text(UiTextId::SettingsCheckUpdates));
        set_secondary_text(secondary_items,
                           kSystemSettingsNetworkDiagItem,
                           settings_display_text(UiTextId::SettingsNetworkDiagnostics));
    }
}
