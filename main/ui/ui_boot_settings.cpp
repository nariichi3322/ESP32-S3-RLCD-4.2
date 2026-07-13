// 构建并刷新设置页及其菜单交互。
#include "ui_views.h"

#include "alarm_services.h"
#include "app_constexpr.h"
#include "app_tick_time.h"

#include "audio_services.h"
#include "network_services.h"
#include "ota_services.h"
#include "sensor_services.h"
#include "ui_settings_ota_panel.h"
#include "ui_text_format.h"

#include <stdarg.h>

namespace {
int collect_visible_work_page_order_indices(int *indices, size_t capacity)
{
    if (!indices || capacity == 0) {
        return 0;
    }
    int count = 0;
    for (int order_index = 0; order_index < kWorkPageCount && (size_t)count < capacity; ++order_index) {
        if (is_work_page_enabled(g_work_page_order[order_index])) {
            indices[count++] = order_index;
        }
    }
    return count;
}

#define SETTINGS_PRIMARY_LABEL_CREATE_FAILED_FORMAT "settings primary label create failed index=%d"
#define SETTINGS_SECONDARY_LABEL_CREATE_FAILED_FORMAT "settings secondary label create failed index=%d"
#define SETTINGS_SECONDARY_FORMAT_FAILED_FORMAT "settings secondary text format failed index=%d"
#define SETTINGS_SWITCH_DOT_CREATE_FAILED_FORMAT "settings switch dot create failed index=%d"
#define SETTINGS_SWITCH_TEXT_CREATE_FAILED_FORMAT "settings switch text create failed index=%d"

constexpr int kSettingsPrimaryX = 12;
constexpr int kSettingsPrimaryW = 112;
constexpr int kSettingsSecondaryX = 150;
constexpr int kSettingsSecondaryW = 228;
constexpr int kSettingsSecondaryH = 30;
constexpr int kSettingsSwitchDotX = 362;
constexpr int kSettingsSwitchDotYOffset = 8;
constexpr int kSettingsSwitchDotSize = 12;
constexpr int kSettingsSwitchTextX = 352;
constexpr int kSettingsSwitchTextYOffset = 6;
constexpr int kSettingsSwitchTextW = 28;
constexpr int kSettingsSwitchTextH = 18;
constexpr size_t kSettingsSecondaryTextSize = 56;
constexpr int kSettingsListRowY[] = {66, 105, 144, 183, 222, 222, 222};
constexpr int kSettingsGridRowY[] = {66, 105, 144, 183};
constexpr size_t kSettingsListRowCount = array_count(kSettingsListRowY);
constexpr size_t kSettingsGridRowCount = array_count(kSettingsGridRowY);
constexpr int kSettingsGridColumns = 2;
constexpr int kSettingsGridLeftX = 150;
constexpr int kSettingsGridRightX = 267;
constexpr int kSettingsGridColW = 111;
constexpr int kSettingsGridSwitchDotXOffset = 92;
constexpr int kSettingsGridSwitchDotYOffset = 9;
constexpr int kSettingsGridSwitchTextSystemXOffset = 80;
constexpr int kSettingsGridSwitchTextDisplayXOffset = 90;
constexpr int kSettingsGridSwitchTextSystemW = 26;
constexpr int kSettingsGridSwitchTextDisplayW = 30;
constexpr int kSettingsGridSwitchTextYOffset = 7;
constexpr int kSettingsSystemLongItemY = 144;
constexpr int kSettingsDisplayLongItemY = 183;
constexpr const char *kSettingsPrimaryItems[kSettingsPrimaryCount] = {"网络", "声音", "显示", "系统"};
constexpr const char *kSettingsNetworkSyncTimeText = "同步时间";
constexpr const char *kSettingsNetworkSyncWeatherText = "同步天气";
constexpr const char *kSettingsNetworkSayingText = "更新一言";
constexpr const char *kSettingsWeatherCityManualFormat = "天气城市 %s";
constexpr const char *kSettingsWeatherCityAutoText = "天气城市 自动";
constexpr const char *kSettingsSoundVolumeFormat = "音量 %d%%";
constexpr const char *kSettingsSoundChoiceFormat = "声音选择 %d";
constexpr const char *kSettingsHourlyText = "整点提醒 7:00 - 22:00";
constexpr const char *kSettingsAllDayText = "全天提醒 0:00 - 24:00";
constexpr const char *kSettingsPageSwitchText = "页面开关";
constexpr const char *kSettingsPageOrderText = "页面顺序";
constexpr const char *kSettingsAlarmOffText = "闹钟 --:--";
constexpr const char *kSettingsAlarmOnFormat = "闹钟 %02d:%02d";
constexpr const char *kSettingsXiaozhiAutoReturnText = "小智AI自动返回";
constexpr const char *kSettingsPageOrderEntryFormat = "%d %s";
constexpr const char *kSettingsOfflineFormat = "离线模式 %s";
constexpr const char *kSettingsOfflineOnText = "开";
constexpr const char *kSettingsOfflineOffText = "关";
constexpr const char *kSettingsNetworkDiagText = "网络检测";
constexpr const char *kSettingsFactoryResetConfirmText = "确认恢复";
constexpr const char *kSettingsFactoryResetText = "恢复出厂设置";
constexpr const char *kSettingsSystemInfoText = "关于本机";
constexpr const char *kSettingsCheckUpdateText = "检查更新";
constexpr const char *kSettingsSecondaryTexts[] = {
    kSettingsNetworkSyncTimeText,
    kSettingsNetworkSyncWeatherText,
    kSettingsNetworkSayingText,
    kSettingsWeatherCityManualFormat,
    kSettingsWeatherCityAutoText,
    kSettingsSoundVolumeFormat,
    kSettingsSoundChoiceFormat,
    kSettingsHourlyText,
    kSettingsAllDayText,
    kSettingsPageSwitchText,
    kSettingsPageOrderText,
    kSettingsAlarmOffText,
    kSettingsAlarmOnFormat,
    kSettingsXiaozhiAutoReturnText,
    kSettingsPageOrderEntryFormat,
    kSettingsOfflineFormat,
    kSettingsOfflineOnText,
    kSettingsOfflineOffText,
    kSettingsNetworkDiagText,
    kSettingsFactoryResetConfirmText,
    kSettingsFactoryResetText,
    kSettingsSystemInfoText,
    kSettingsCheckUpdateText,
};
#define SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT "settings secondary text index out of range: %d"
#define SETTINGS_SWITCH_SLOT_INDEX_OUT_OF_RANGE_FORMAT "settings switch slot index out of range: %d"
constexpr const char *kLabelCreateFailedLog = "label create failed";
constexpr const char *kSettingsLabelPlaceholder = "--";
constexpr const char *kSettingsFixedTexts[] = {
    kLabelCreateFailedLog,
    kSettingsLabelPlaceholder,
};
constexpr const char *kBootSettingsLogTexts[] = {
    SETTINGS_PRIMARY_LABEL_CREATE_FAILED_FORMAT,
    SETTINGS_SECONDARY_LABEL_CREATE_FAILED_FORMAT,
    SETTINGS_SECONDARY_FORMAT_FAILED_FORMAT,
    SETTINGS_SWITCH_DOT_CREATE_FAILED_FORMAT,
    SETTINGS_SWITCH_TEXT_CREATE_FAILED_FORMAT,
    SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT,
    SETTINGS_SWITCH_SLOT_INDEX_OUT_OF_RANGE_FORMAT,
};

struct SettingsGridCell {
    int x;
    int y;
};

struct SettingsGridSwitchTextLayout {
    int x_offset;
    int w;
};

SettingsGridCell settings_grid_cell(int index)
{
    int col = index % kSettingsGridColumns;
    int row = index / kSettingsGridColumns;
    return {
        col == 0 ? kSettingsGridLeftX : kSettingsGridRightX,
        kSettingsGridRowY[row],
    };
}

SettingsGridSwitchTextLayout settings_grid_switch_text_layout(int primary)
{
    return primary == kSettingsPrimarySystem
               ? SettingsGridSwitchTextLayout{kSettingsGridSwitchTextSystemXOffset,
                                              kSettingsGridSwitchTextSystemW}
               : SettingsGridSwitchTextLayout{kSettingsGridSwitchTextDisplayXOffset,
                                              kSettingsGridSwitchTextDisplayW};
}

int settings_long_item_y(int primary)
{
    return primary == kSettingsPrimarySystem ? kSettingsSystemLongItemY : kSettingsDisplayLongItemY;
}

static_assert(kSettingsListRowCount == kSettingsSecondaryMaxCount,
              "settings list rows must match secondary slot count");
static_assert(kSettingsGridRowCount * kSettingsGridColumns >= kWorkPageCount,
              "settings grid capacity must cover all work pages");
static_assert(kSettingsGridRowCount * kSettingsGridColumns >= kSystemSettingsGridItemCount,
              "settings grid capacity must cover system grid items");
static_assert(array_count(kSettingsPrimaryItems) == kSettingsPrimaryCount,
              "settings primary item table must match primary count");
static_assert(array_count(g_settings_labels) == kSettingsLabelCount,
              "settings label storage must match configured label count");
static_assert(array_count(g_settings_switch_dots) == kSettingsSecondaryMaxCount,
              "settings switch dot storage must match secondary slot count");
static_assert(array_count(g_settings_switch_texts) == kSettingsSecondaryMaxCount,
              "settings switch text storage must match secondary slot count");
static_assert(array_count(kSettingsSecondaryTexts) > 0, "settings secondary text registry must not be empty");
static_assert(array_count(kSettingsFixedTexts) > 0, "settings fixed text registry must not be empty");
static_assert(array_count(kBootSettingsLogTexts) > 0, "boot/settings log registry must not be empty");
static_assert(cstr_array_nonempty(kSettingsPrimaryItems), "settings primary menu texts must be non-empty");
static_assert(cstr_array_nonempty(kSettingsSecondaryTexts), "settings secondary menu texts must be non-empty");
static_assert(cstr_array_nonempty(kSettingsFixedTexts), "settings fixed texts must be non-empty");
static_assert(cstr_array_nonempty(kBootSettingsLogTexts), "boot/settings log texts must be non-empty");
static_assert(kSettingsSecondaryTextSize > 1, "settings secondary text buffer must fit text and NUL");
static_assert(kSettingsGridColumns > 0, "settings grid must have columns");
static_assert(kSettingsGridColW > 0 && kSettingsSecondaryH > 0,
              "settings grid item size must be positive");
static_assert(kSettingsSystemLongItemY >= 0 && kSettingsDisplayLongItemY >= 0,
              "settings long item y positions must be non-negative");
static_assert(kSettingsListRowCount >= kSettingsPrimaryCount,
              "settings list rows must fit primary menu items");

bool settings_secondary_index_valid(int index)
{
    return index >= 0 && index < kSettingsSecondaryMaxCount;
}

void set_secondary_text(char items[][kSettingsSecondaryTextSize], int index, const char *text)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    ui_text::copy(items[index], kSettingsSecondaryTextSize, text);
}

void format_secondary_text(char items[][kSettingsSecondaryTextSize], int index, const char *format, ...)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SECONDARY_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    items[index][0] = '\0';
    va_list args;
    va_start(args, format);
    int written = vsnprintf(items[index], kSettingsSecondaryTextSize, format ? format : "", args);
    va_end(args);
    if (ui_text::format_failed(written, kSettingsSecondaryTextSize)) {
        items[index][0] = '\0';
        ESP_LOGW(TAG, SETTINGS_SECONDARY_FORMAT_FAILED_FORMAT, index);
    }
}

void hide_settings_switch_slot(int index)
{
    if (!settings_secondary_index_valid(index)) {
        ESP_LOGW(TAG, SETTINGS_SWITCH_SLOT_INDEX_OUT_OF_RANGE_FORMAT, index);
        return;
    }
    if (g_settings_switch_dots[index]) {
        set_obj_visible(g_settings_switch_dots[index], false);
    }
    if (g_settings_switch_texts[index]) {
        set_obj_visible(g_settings_switch_texts[index], false);
    }
}

void warn_if_center_align_failed(lv_obj_t *label, const char *message)
{
    if (!center_align_label(label)) {
        ESP_LOGW(TAG, "%s", cstr_nonempty(message) ? message : kLabelCreateFailedLog);
    }
}

}

void style_settings_item(lv_obj_t *label, bool selected)
{
    lv_obj_set_style_bg_color(label, selected ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, selected ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(label, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label, 5, LV_PART_MAIN);
}

static void style_settings_switch_dot(lv_obj_t *dot, bool on, bool selected)
{
    if (!dot) {
        return;
    }
    lv_color_t fg = selected ? lv_color_white() : lv_color_black();
    lv_color_t bg = selected ? lv_color_black() : lv_color_white();
    lv_obj_set_style_bg_color(dot, on ? fg : bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dot, fg, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
}

void build_settings_page()
{
    if (g_settings_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_settings_root = screen;
    lv_obj_add_flag(g_settings_root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = make_label(screen, 24, 18, 352, 28, "设置");
    warn_if_center_align_failed(title, "settings title create failed");
    make_black_bar(screen, 24, 52, 352, 3);

    make_black_bar(screen, 136, 62, 2, 174);

    for (int i = 0; i < kSettingsPrimaryCount; ++i) {
        g_settings_labels[i] =
            make_label(screen, kSettingsPrimaryX, kSettingsListRowY[i], kSettingsPrimaryW, kSettingsSecondaryH, kSettingsLabelPlaceholder);
        if (g_settings_labels[i]) {
            lv_label_set_long_mode(g_settings_labels[i], LV_LABEL_LONG_CLIP);
            center_align_label(g_settings_labels[i]);
        } else {
            ESP_LOGW(TAG, SETTINGS_PRIMARY_LABEL_CREATE_FAILED_FORMAT, i);
        }
    }
    for (int i = 0; i < kSettingsSecondaryMaxCount; ++i) {
        int slot = kSettingsPrimaryCount + i;
        g_settings_labels[slot] =
            make_label(screen, kSettingsSecondaryX, kSettingsListRowY[i], kSettingsSecondaryW, kSettingsSecondaryH, kSettingsLabelPlaceholder);
        if (g_settings_labels[slot]) {
            lv_label_set_long_mode(g_settings_labels[slot], LV_LABEL_LONG_CLIP);
            center_align_label(g_settings_labels[slot]);
        } else {
            ESP_LOGW(TAG, SETTINGS_SECONDARY_LABEL_CREATE_FAILED_FORMAT, i);
        }
        g_settings_switch_dots[i] = lv_obj_create(screen);
        if (g_settings_switch_dots[i]) {
            lv_obj_clear_flag(g_settings_switch_dots[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(g_settings_switch_dots[i],
                           kSettingsSwitchDotX,
                           kSettingsListRowY[i] + kSettingsSwitchDotYOffset);
            lv_obj_set_size(g_settings_switch_dots[i], kSettingsSwitchDotSize, kSettingsSwitchDotSize);
            style_settings_switch_dot(g_settings_switch_dots[i], false, false);
            lv_obj_add_flag(g_settings_switch_dots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW(TAG, SETTINGS_SWITCH_DOT_CREATE_FAILED_FORMAT, i);
        }
        g_settings_switch_texts[i] =
            make_label(screen,
                       kSettingsSwitchTextX,
                       kSettingsListRowY[i] + kSettingsSwitchTextYOffset,
                       kSettingsSwitchTextW,
                       kSettingsSwitchTextH,
                       "");
        if (g_settings_switch_texts[i]) {
            center_align_label(g_settings_switch_texts[i]);
            lv_obj_set_style_pad_all(g_settings_switch_texts[i], 0, LV_PART_MAIN);
            lv_label_set_long_mode(g_settings_switch_texts[i], LV_LABEL_LONG_CLIP);
            lv_obj_add_flag(g_settings_switch_texts[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW(TAG, SETTINGS_SWITCH_TEXT_CREATE_FAILED_FORMAT, i);
        }
    }
    build_settings_ota_panel(screen, kSettingsSecondaryX, kSettingsSecondaryW);

    g_settings_feedback_label = make_label(screen, 24, 246, 352, 20, "");
    warn_if_center_align_failed(g_settings_feedback_label, "settings feedback label create failed");

    lv_obj_t *hint = make_label(screen, 24, 270, 352, 22, "KEY选择  长按返回  BOOT确认");
    warn_if_center_align_failed(hint, "settings hint label create failed");
}

namespace {
void populate_settings_secondary_items(
    int primary,
    char secondary_items[][kSettingsSecondaryTextSize])
{
    if (primary == kSettingsPrimaryNetwork) {
        set_secondary_text(secondary_items, kNetworkSettingsNtpItem, kSettingsNetworkSyncTimeText);
        set_secondary_text(secondary_items, kNetworkSettingsWeatherItem, kSettingsNetworkSyncWeatherText);
        set_secondary_text(secondary_items, kNetworkSettingsSayingItem, kSettingsNetworkSayingText);
        if (g_has_manual_weather_city) {
            format_secondary_text(secondary_items,
                                  kNetworkSettingsWeatherCityItem,
                                  kSettingsWeatherCityManualFormat,
                                  g_manual_weather_city);
        } else {
            set_secondary_text(secondary_items, kNetworkSettingsWeatherCityItem, kSettingsWeatherCityAutoText);
        }
    } else if (primary == kSettingsPrimarySound) {
        format_secondary_text(secondary_items,
                              kSoundSettingsVolumeItem,
                              kSettingsSoundVolumeFormat,
                              g_chime_volume_percent);
        format_secondary_text(secondary_items,
                              kSoundSettingsSoundItem,
                              kSettingsSoundChoiceFormat,
                              g_chime_sound_index + 1);
        set_secondary_text(secondary_items, kSoundSettingsHourlyItem, kSettingsHourlyText);
        set_secondary_text(secondary_items, kSoundSettingsAllDayItem, kSettingsAllDayText);
    } else if (primary == kSettingsPrimaryDisplay) {
        set_secondary_text(secondary_items, kDisplaySettingsPageSwitchItem, kSettingsPageSwitchText);
        set_secondary_text(secondary_items, kDisplaySettingsOrderItem, kSettingsPageOrderText);
        AlarmSnapshot alarm = {};
        alarm_get_snapshot(&alarm);
        if (alarm.enabled) {
            format_secondary_text(secondary_items,
                                  kDisplaySettingsAlarmItem,
                                  kSettingsAlarmOnFormat,
                                  alarm.hour,
                                  alarm.minute);
        } else {
            set_secondary_text(secondary_items, kDisplaySettingsAlarmItem, kSettingsAlarmOffText);
        }
        set_secondary_text(secondary_items,
                           kDisplaySettingsXiaozhiAutoReturnItem,
                           kSettingsXiaozhiAutoReturnText);
    } else {
        format_secondary_text(secondary_items,
                              kSystemSettingsOfflineItem,
                              kSettingsOfflineFormat,
                              g_offline_mode_ui_enabled ? kSettingsOfflineOnText : kSettingsOfflineOffText);
        set_secondary_text(secondary_items, kSystemSettingsNetworkDiagItem, kSettingsNetworkDiagText);
        set_secondary_text(secondary_items,
                           kSystemSettingsFactoryResetItem,
                           g_factory_reset_confirm_pending ? kSettingsFactoryResetConfirmText
                                                           : kSettingsFactoryResetText);
        set_secondary_text(secondary_items, kSystemSettingsInfoItem, kSettingsSystemInfoText);
        set_secondary_text(secondary_items, kSystemSettingsOtaItem, kSettingsCheckUpdateText);
    }
}

bool settings_render_selection_changed(int primary, int selected)
{
    static lv_obj_t *last_settings_root = nullptr;
    static int last_primary = -1;
    static int last_selected = -1;
    static bool last_focus_secondary = false;
    static int last_ota_state = -1;
    static int last_ota_progress = -2;
    static int last_ota_speed = -2;
    static bool last_page_order_mode = false;
    static bool last_page_toggle_mode = false;
    static int last_page_order_selection = -1;
    bool selection_changed = g_settings_root != last_settings_root ||
                             selected != last_selected ||
                             primary != last_primary ||
                             g_settings_focus_secondary != last_focus_secondary ||
                             g_settings_page_toggle_mode != last_page_toggle_mode ||
                             g_settings_page_order_mode != last_page_order_mode ||
                             g_settings_page_order_selection != last_page_order_selection ||
                             g_ota_state != last_ota_state ||
                             g_ota_progress != last_ota_progress ||
                             g_ota_speed_kbps != last_ota_speed;
    if (selection_changed) {
        last_settings_root = g_settings_root;
        last_selected = selected;
        last_primary = primary;
        last_focus_secondary = g_settings_focus_secondary;
        last_page_toggle_mode = g_settings_page_toggle_mode;
        last_page_order_mode = g_settings_page_order_mode;
        last_page_order_selection = g_settings_page_order_selection;
        last_ota_state = g_ota_state;
        last_ota_progress = g_ota_progress;
        last_ota_speed = g_ota_speed_kbps;
    }
    return selection_changed;
}

bool update_settings_primary_items(int primary, bool selection_changed)
{
    bool changed = false;
    for (int i = 0; i < kSettingsPrimaryCount; ++i) {
        if (g_settings_labels[i]) {
            changed |= set_label_text_if_changed(g_settings_labels[i], kSettingsPrimaryItems[i]);
            if (selection_changed) {
                style_settings_item(g_settings_labels[i], i == primary);
            }
        }
    }
    return changed;
}

bool layout_settings_secondary_slot(
    int index,
    int primary,
    int visible_order_count,
    const int *visible_order_indices,
    char secondary_items[][kSettingsSecondaryTextSize])
{
    int slot = kSettingsPrimaryCount + index;
    if (g_settings_page_order_mode || g_settings_page_toggle_mode) {
        int manager_item_count = g_settings_page_order_mode ? visible_order_count : kWorkPageCount;
        if (index >= manager_item_count) {
            set_obj_visible(g_settings_labels[slot], false);
            hide_settings_switch_slot(index);
            return false;
        }
        SettingsGridCell cell = settings_grid_cell(index);
        lv_obj_set_pos(g_settings_labels[slot], cell.x, cell.y);
        lv_obj_set_size(g_settings_labels[slot], kSettingsGridColW, kSettingsSecondaryH);
        if (g_settings_page_order_mode) {
            int order_index = visible_order_indices[index];
            format_secondary_text(secondary_items,
                                  index,
                                  kSettingsPageOrderEntryFormat,
                                  index + 1,
                                  work_page_name(g_work_page_order[order_index]));
            hide_settings_switch_slot(index);
        } else {
            set_secondary_text(secondary_items, index, work_page_name(index));
            if (g_settings_switch_dots[index]) {
                lv_obj_set_pos(g_settings_switch_dots[index],
                               cell.x + kSettingsGridSwitchDotXOffset,
                               cell.y + kSettingsGridSwitchDotYOffset);
            }
        }
    } else if (primary == kSettingsPrimarySystem) {
        bool grid_item = index < kSystemSettingsGridItemCount;
        if (grid_item) {
            SettingsGridCell cell = settings_grid_cell(index);
            lv_obj_set_pos(g_settings_labels[slot], cell.x, cell.y);
            lv_obj_set_size(g_settings_labels[slot], kSettingsGridColW, kSettingsSecondaryH);
            if (g_settings_switch_dots[index]) {
                lv_obj_set_pos(g_settings_switch_dots[index],
                               cell.x + kSettingsGridSwitchDotXOffset,
                               cell.y + kSettingsGridSwitchDotYOffset);
            }
            if (g_settings_switch_texts[index]) {
                SettingsGridSwitchTextLayout text_layout = settings_grid_switch_text_layout(primary);
                lv_obj_set_pos(g_settings_switch_texts[index],
                               cell.x + text_layout.x_offset,
                               cell.y + kSettingsGridSwitchTextYOffset);
                lv_obj_set_size(g_settings_switch_texts[index], text_layout.w, kSettingsSwitchTextH);
            }
        } else {
            lv_obj_set_pos(g_settings_labels[slot],
                           kSettingsSecondaryX,
                           settings_long_item_y(primary));
            lv_obj_set_size(g_settings_labels[slot], kSettingsSecondaryW, kSettingsSecondaryH);
            hide_settings_switch_slot(index);
        }
    } else {
        lv_obj_set_pos(g_settings_labels[slot], kSettingsSecondaryX, kSettingsListRowY[index]);
        lv_obj_set_size(g_settings_labels[slot], kSettingsSecondaryW, kSettingsSecondaryH);
        if (g_settings_switch_dots[index]) {
            lv_obj_set_pos(g_settings_switch_dots[index],
                           kSettingsSwitchDotX,
                           kSettingsListRowY[index] + kSettingsSwitchDotYOffset);
        }
        if (g_settings_switch_texts[index]) {
            lv_obj_set_pos(g_settings_switch_texts[index],
                           kSettingsSwitchTextX,
                           kSettingsListRowY[index] + kSettingsSwitchTextYOffset);
            lv_obj_set_size(g_settings_switch_texts[index], kSettingsSwitchTextW, kSettingsSwitchTextH);
        }
    }
    return true;
}

void update_settings_switch_slot(int index, int primary, int selected, bool visible)
{
    bool dot_visible = false;
    bool dot_on = false;
    bool switch_text_visible = false;
    const char *switch_text = "";
    if (visible && primary == kSettingsPrimarySound) {
        if (index >= kSoundSettingsHourlyItem) {
            dot_visible = true;
            dot_on = index == kSoundSettingsHourlyItem ? g_hourly_chime_enabled : g_hourly_chime_all_day;
        }
    } else if (visible &&
               primary == kSettingsPrimaryDisplay &&
               g_settings_page_toggle_mode) {
        dot_visible = true;
        dot_on = is_work_page_enabled(index);
    } else if (visible &&
               primary == kSettingsPrimaryDisplay &&
               !g_settings_page_toggle_mode &&
               !g_settings_page_order_mode &&
               (index == kDisplaySettingsAlarmItem ||
                index == kDisplaySettingsXiaozhiAutoReturnItem)) {
        dot_visible = true;
        dot_on = index == kDisplaySettingsAlarmItem
                     ? alarm_is_enabled()
                     : g_xiaozhi_auto_return_enabled;
    }
    if (g_settings_switch_dots[index]) {
        set_obj_visible(g_settings_switch_dots[index], dot_visible);
        if (dot_visible) {
            style_settings_switch_dot(g_settings_switch_dots[index],
                                      dot_on,
                                      g_settings_focus_secondary && index == selected);
        }
    }
    if (g_settings_switch_texts[index]) {
        set_obj_visible(g_settings_switch_texts[index], switch_text_visible);
        if (switch_text_visible) {
            set_label_text_if_changed(g_settings_switch_texts[index], switch_text);
            bool selected_item = g_settings_focus_secondary && index == selected;
            lv_obj_set_style_text_color(g_settings_switch_texts[index],
                                        selected_item ? lv_color_white() : lv_color_black(),
                                        LV_PART_MAIN);
        }
    }
}

bool update_settings_secondary_items(
    int primary,
    int selected,
    bool selection_changed,
    char secondary_items[][kSettingsSecondaryTextSize])
{
    bool changed = false;
    int secondary_count = settings_secondary_count(primary);
    int visible_order_indices[kWorkPageCount] = {};
    int visible_order_count = g_settings_page_order_mode
                                  ? collect_visible_work_page_order_indices(visible_order_indices,
                                                                            array_count(visible_order_indices))
                                  : 0;
    for (int i = 0; i < kSettingsSecondaryMaxCount; ++i) {
        int slot = kSettingsPrimaryCount + i;
        if (!g_settings_labels[slot]) {
            continue;
        }
        if (!layout_settings_secondary_slot(i,
                                            primary,
                                            visible_order_count,
                                            visible_order_indices,
                                            secondary_items)) {
            continue;
        }
        bool visible = i < secondary_count;
        if (g_settings_page_order_mode) {
            visible = i < visible_order_count;
        } else if (g_settings_page_toggle_mode) {
            visible = i < kWorkPageCount;
        }
        set_obj_visible(g_settings_labels[slot], visible);
        if (visible) {
            changed |= set_label_text_if_changed(g_settings_labels[slot], secondary_items[i]);
            if (selection_changed) {
                int order_index = g_settings_page_order_mode ? visible_order_indices[i] : -1;
                bool selected_item = g_settings_page_order_mode ? order_index == g_settings_page_order_selection :
                                     g_settings_page_toggle_mode ? i == selected :
                                     (g_settings_focus_secondary && i == selected);
                style_settings_item(g_settings_labels[slot], selected_item);
                if (primary == kSettingsPrimarySystem && i < kSystemSettingsGridItemCount) {
                    lv_obj_set_style_pad_left(g_settings_labels[slot], 4, LV_PART_MAIN);
                    lv_obj_set_style_pad_right(g_settings_labels[slot], 4, LV_PART_MAIN);
                }
            }
        }
        update_settings_switch_slot(i, primary, selected, visible);
    }
    return changed;
}

bool update_settings_feedback_label()
{
    if (!g_settings_feedback_label) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if (g_settings_feedback[0] &&
        g_settings_feedback_until_tick != 0 &&
        app_tick_deadline_pending(now, g_settings_feedback_until_tick)) {
        return set_label_text_if_changed(g_settings_feedback_label, g_settings_feedback);
    }
    g_settings_feedback[0] = '\0';
    return set_label_text_if_changed(g_settings_feedback_label, "");
}
} // namespace

bool update_settings_page()
{
    ota_reset_status_if_idle();
    char secondary_items[kSettingsSecondaryMaxCount][kSettingsSecondaryTextSize] = {};
    int primary = clamp_settings_primary(g_settings_primary_selection);
    int selected = clamp_settings_selection_for_mode(primary,
                                                     g_settings_selection,
                                                     g_settings_page_toggle_mode);
    g_settings_primary_selection = primary;
    g_settings_selection = selected;
    if (g_settings_page_order_mode) {
        normalize_work_page_order();
        g_settings_page_order_selection =
            valid_enabled_work_page_order_index(g_settings_page_order_selection);
    }

    populate_settings_secondary_items(primary, secondary_items);
    bool selection_changed = settings_render_selection_changed(primary, selected);
    bool changed = selection_changed;
    changed |= update_settings_primary_items(primary, selection_changed);
    changed |= update_settings_secondary_items(primary,
                                               selected,
                                               selection_changed,
                                               secondary_items);
    bool ota_panel_visible = primary == kSettingsPrimarySystem && selected == kSystemSettingsOtaItem;
    changed |= update_settings_ota_panel(ota_panel_visible);
    changed |= update_settings_feedback_label();
    return changed;
}
