// 验证设置页四类二级菜单文案及安全索引边界。
#include "ui_settings_content.h"

#include "alarm_services.h"
#include "app_state.h"

#include <assert.h>
#include <string.h>

const char *const TAG = "test";
char g_manual_weather_city[kManualWeatherCityLen] = {};
bool g_has_manual_weather_city = false;
bool g_offline_mode_ui_enabled = false;
int g_chime_volume_percent = 60;
int g_chime_sound_index = 2;
bool g_factory_reset_confirm_pending = false;

namespace {
AlarmSnapshot s_alarm = {};

void expect_text(char items[][kSettingsSecondaryTextSize], int index, const char *expected)
{
    assert(strcmp(items[index], expected) == 0);
}
} // namespace

void alarm_get_snapshot(AlarmSnapshot *out)
{
    if (out) {
        *out = s_alarm;
    }
}

int main()
{
    char items[kSettingsSecondaryMaxCount][kSettingsSecondaryTextSize] = {};

    assert(!settings_secondary_index_valid(-1));
    assert(settings_secondary_index_valid(0));
    assert(settings_secondary_index_valid(kSettingsSecondaryMaxCount - 1));
    assert(!settings_secondary_index_valid(kSettingsSecondaryMaxCount));

    populate_settings_secondary_items(kSettingsPrimaryNetwork, items);
    expect_text(items, kNetworkSettingsNtpItem, "同步时间");
    expect_text(items, kNetworkSettingsWeatherItem, "同步天气");
    expect_text(items, kNetworkSettingsSayingItem, "更新一言");
    expect_text(items, kNetworkSettingsWeatherCityItem, "天气城市 自动");

    g_has_manual_weather_city = true;
    strlcpy(g_manual_weather_city, "杭州", sizeof(g_manual_weather_city));
    memset(items, 0, sizeof(items));
    populate_settings_secondary_items(kSettingsPrimaryNetwork, items);
    expect_text(items, kNetworkSettingsWeatherCityItem, "天气城市 杭州");

    memset(items, 0, sizeof(items));
    populate_settings_secondary_items(kSettingsPrimarySound, items);
    expect_text(items, kSoundSettingsVolumeItem, "音量 60%");
    expect_text(items, kSoundSettingsSoundItem, "声音选择 3");
    expect_text(items, kSoundSettingsHourlyItem, "整点提醒 7:00 - 22:00");
    expect_text(items, kSoundSettingsAllDayItem, "全天提醒 0:00 - 24:00");

    memset(items, 0, sizeof(items));
    populate_settings_secondary_items(kSettingsPrimaryDisplay, items);
    expect_text(items, kDisplaySettingsPageSwitchItem, "页面开关");
    expect_text(items, kDisplaySettingsOrderItem, "页面顺序");
    expect_text(items, kDisplaySettingsAlarmItem, "闹钟 --:--");
    expect_text(items, kDisplaySettingsXiaozhiAutoReturnItem, "小智AI自动返回");

    s_alarm.enabled = true;
    s_alarm.hour = 6;
    s_alarm.minute = 30;
    memset(items, 0, sizeof(items));
    populate_settings_secondary_items(kSettingsPrimaryDisplay, items);
    expect_text(items, kDisplaySettingsAlarmItem, "闹钟 06:30");

    g_offline_mode_ui_enabled = true;
    g_factory_reset_confirm_pending = true;
    memset(items, 0, sizeof(items));
    populate_settings_secondary_items(kSettingsPrimarySystem, items);
    expect_text(items, kSystemSettingsOfflineItem, "离线模式 开");
    expect_text(items, kSystemSettingsNetworkDiagItem, "网络检测");
    expect_text(items, kSystemSettingsFactoryResetItem, "确认恢复");
    expect_text(items, kSystemSettingsInfoItem, "关于本机");
    expect_text(items, kSystemSettingsOtaItem, "检查更新");

    return 0;
}
