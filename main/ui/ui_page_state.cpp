// 管理页面根对象、可见性、工作页顺序和低电量显示状态。
#include "ui_views.h"

#include "network_services.h"

namespace {
#define PAGE_ROOT_CREATE_FAILED_LOG "page root create failed"
constexpr int kFirstWorkPage = kWorkPageWeatherClock;
constexpr int kFallbackWorkPage = kWorkPageWeatherClock;
constexpr size_t kAuxPageRootCount = 3; // System info, network diagnostics and settings.
constexpr uint8_t kDefaultWorkPageOrder[kWorkPageCount] = {
    kWorkPageWeatherClock,
    kWorkPageGallery,
    kWorkPageWeatherBoard,
    kWorkPageFlipClock,
    kWorkPageCalendar,
    kWorkPageHistory,
};
constexpr int kDisplaySettingPages[kDisplaySettingsPageItemCount] = {
    kWorkPageWeatherClock,
    kWorkPageGallery,
    kWorkPageWeatherBoard,
    kWorkPageFlipClock,
    kWorkPageCalendar,
    kWorkPageHistory,
};
constexpr const char *kWorkPageNames[kWorkPageCount] = {
    "天气时钟",
    "图片时钟",
    "天气看板",
    "温湿时钟",
    "日历",
    "温湿历史",
};
constexpr const char *kUnknownWorkPageName = "未知页面";

bool is_work_page_index(int page)
{
    return page >= kFirstWorkPage && page < kWorkPageCount;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

template <typename T, size_t N>
constexpr bool page_list_covers_each_work_page_once(const T (&pages)[N])
{
    if (N != kWorkPageCount) {
        return false;
    }
    for (int page = kFirstWorkPage; page < kWorkPageCount; ++page) {
        int hits = 0;
        for (size_t i = 0; i < N; ++i) {
            if (pages[i] == page) {
                ++hits;
            }
        }
        if (hits != 1) {
            return false;
        }
    }
    return true;
}

template <typename T, size_t N>
void clear_pointer_array(T *(&items)[N])
{
    for (T *&item : items) {
        item = nullptr;
    }
}

lv_obj_t *work_page_root_or_fallback(lv_obj_t *root)
{
    return root ? root : g_clock_root;
}

lv_obj_t *build_work_page_root(int page)
{
    switch (page) {
    case kWorkPageWeatherClock:
        build_clock_ui();
        return g_clock_root;
    case kWorkPageHistory:
        build_history_page();
        return work_page_root_or_fallback(g_history_root);
    case kWorkPageGallery:
        build_gallery_page();
        return work_page_root_or_fallback(g_gallery_root);
    case kWorkPageCalendar:
        build_calendar_page();
        return work_page_root_or_fallback(g_calendar_root);
    case kWorkPageWeatherBoard:
        build_weather_board_page();
        return work_page_root_or_fallback(g_weather_board_root);
    case kWorkPageFlipClock:
        build_flip_clock_page();
        return work_page_root_or_fallback(g_flip_clock_root);
    default:
        return g_clock_root;
    }
}

static_assert(kFirstWorkPage == 0, "work page ids must start at zero");
static_assert(kFallbackWorkPage == kWorkPageWeatherClock, "fallback page must remain weather clock");
static_assert(kAuxPageRootCount == 3, "auxiliary roots are info, network diagnostics and settings");
static_assert(kWorkPageCount > 0, "there must be at least one work page");
static_assert(kWorkPageCount < static_cast<int>(sizeof(uint32_t) * 8),
              "work page enabled mask must have room for every work page bit");
static_assert(array_count(kDefaultWorkPageOrder) == kWorkPageCount,
              "default work page order must cover every work page");
static_assert(array_count(kDisplaySettingPages) == kDisplaySettingsPageItemCount,
              "display setting page mapping must match the settings item count");
static_assert(array_count(kWorkPageNames) == kWorkPageCount,
              "work page names must cover every work page");
static_assert(cstr_array_nonempty(kWorkPageNames), "work page names must be non-empty");
static_assert(cstr_nonempty(kUnknownWorkPageName), "unknown work page name must be non-empty");
static_assert(page_list_covers_each_work_page_once(kDefaultWorkPageOrder),
              "default work page order must include every work page exactly once");
static_assert(page_list_covers_each_work_page_once(kDisplaySettingPages),
              "display settings must map every work page exactly once");
} // namespace

lv_obj_t *create_page_root()
{
    lv_obj_t *root = lv_obj_create(lv_scr_act());
    if (!root) {
        ESP_LOGW(TAG, "%s", PAGE_ROOT_CREATE_FAILED_LOG);
        return nullptr;
    }
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, kDisplayWidth, kDisplayHeight);
    lv_obj_set_style_bg_color(root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    return root;
}

void set_page_visible(lv_obj_t *page, bool visible)
{
    if (!page) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(page);
    } else {
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_page(lv_obj_t *page)
{
    lv_obj_t *roots[] = {
        g_clock_root,
        g_history_root,
        g_gallery_root,
        g_calendar_root,
        g_weather_board_root,
        g_flip_clock_root,
        g_info_root,
        g_network_diag_root,
        g_settings_root,
    };
    constexpr size_t kPageRootCount = array_count(roots);
    static_assert(kPageRootCount == kWorkPageCount + kAuxPageRootCount,
                  "page root visibility list must cover all work pages and auxiliary pages");
    for (lv_obj_t *root : roots) {
        set_page_visible(root, page == root);
    }
}

lv_obj_t *active_work_page_root()
{
    if (g_low_battery_mode || g_setup_portal_active) {
        g_active_work_page = kFallbackWorkPage;
    }
    ensure_active_work_page_enabled();
    return build_work_page_root(g_active_work_page);
}

void show_active_work_page()
{
    show_page(active_work_page_root());
}

bool is_work_page_enabled(int page)
{
    if (page <= kFallbackWorkPage) {
        return true;
    }
    if (page >= kWorkPageCount) {
        return false;
    }
    return (g_work_page_enabled_mask & (1U << page)) != 0;
}

const char *work_page_name(int page)
{
    if (!is_work_page_index(page)) {
        return kUnknownWorkPageName;
    }
    return kWorkPageNames[page];
}

int display_settings_item_work_page(int item)
{
    constexpr int kDisplaySettingPageCount = static_cast<int>(array_count(kDisplaySettingPages));
    if (item < 0 || item >= kDisplaySettingPageCount) {
        return -1;
    }
    return kDisplaySettingPages[item];
}

int first_enabled_work_page()
{
    normalize_work_page_order();
    for (int i = 0; i < kWorkPageCount; ++i) {
        int candidate = g_work_page_order[i];
        if (is_work_page_enabled(candidate)) {
            return candidate;
        }
    }
    return kFallbackWorkPage;
}

void reset_work_page_order()
{
    static_assert(sizeof(kDefaultWorkPageOrder) == sizeof(g_work_page_order),
                  "default work page order storage must match runtime order storage");
    memcpy(g_work_page_order, kDefaultWorkPageOrder, sizeof(g_work_page_order));
}

void normalize_work_page_order()
{
    bool seen[kWorkPageCount] = {};
    for (int i = 0; i < kWorkPageCount; ++i) {
        uint8_t page = g_work_page_order[i];
        if (page >= kWorkPageCount || seen[page]) {
            reset_work_page_order();
            return;
        }
        seen[page] = true;
    }
    for (bool present : seen) {
        if (!present) {
            reset_work_page_order();
            return;
        }
    }
}

int next_enabled_work_page(int current_page)
{
    if (!is_work_page_index(current_page)) {
        current_page = kFallbackWorkPage;
    }
    normalize_work_page_order();
    int current_index = -1;
    for (int i = 0; i < kWorkPageCount; ++i) {
        if (g_work_page_order[i] == current_page) {
            current_index = i;
            break;
        }
    }
    for (int step = 1; step <= kWorkPageCount; ++step) {
        int candidate = g_work_page_order[(current_index + step + kWorkPageCount) % kWorkPageCount];
        if (is_work_page_enabled(candidate)) {
            return candidate;
        }
    }
    return kFallbackWorkPage;
}

void ensure_active_work_page_enabled()
{
    if (!is_work_page_enabled(g_active_work_page)) {
        g_active_work_page = first_enabled_work_page();
    }
}

void clear_clock_object_refs()
{
    g_clock_root = nullptr;
    g_history_root = nullptr;
    g_gallery_root = nullptr;
    g_calendar_root = nullptr;
    g_weather_board_root = nullptr;
    g_flip_clock_root = nullptr;
    g_date_label = nullptr;
    g_clock_summary_label = nullptr;
    g_history_date_label = nullptr;
    g_gallery_date_label = nullptr;
    g_calendar_date_label = nullptr;
    g_weather_board_date_label = nullptr;
    g_flip_clock_date_label = nullptr;
    g_history_summary_label = nullptr;
    g_gallery_summary_label = nullptr;
    g_calendar_summary_label = nullptr;
    g_weather_board_summary_label = nullptr;
    g_flip_clock_summary_label = nullptr;
    g_history_status_time_label = nullptr;
    g_gallery_status_time_label = nullptr;
    g_calendar_status_time_label = nullptr;
    g_weather_board_status_time_label = nullptr;
    g_flip_clock_status_time_label = nullptr;
    clear_pointer_array(g_work_status_chime_icon_canvas);
    clear_pointer_array(g_work_status_wifi_icon_canvas);
    g_gallery_time_label = nullptr;
    g_gallery_hour_label = nullptr;
    g_gallery_minute_label = nullptr;
    g_gallery_image_canvas = nullptr;
    g_gallery_time_canvas = nullptr;
    g_gallery_saying_label = nullptr;
    g_calendar_month_label = nullptr;
    g_calendar_canvas = nullptr;
    g_temp_icon_canvas = nullptr;
    g_humi_icon_canvas = nullptr;
    g_temp_label = nullptr;
    g_humi_label = nullptr;
    g_temp_trend_canvas = nullptr;
    g_humi_trend_canvas = nullptr;
    g_weather_city_label = nullptr;
    g_weather_info_label = nullptr;
    g_weather_icon_label = nullptr;
    g_weather_temp_label = nullptr;
    g_weather_humi_label = nullptr;
    g_alert_pill = nullptr;
    g_alert_icon_canvas = nullptr;
    g_alert_label = nullptr;
    g_chime_status_icon_canvas = nullptr;
    g_wifi_status_icon_canvas = nullptr;
    g_low_battery_icon_canvas = nullptr;
    g_panel_sep_a = nullptr;
    g_panel_sep_b = nullptr;
    g_time_canvas = nullptr;
    g_second_canvas = nullptr;
    g_status_gif_canvas = nullptr;
    g_day_progress_canvas = nullptr;
    g_second_progress_canvas = nullptr;
    clear_pointer_array(g_flip_clock_card_canvas);
    g_flip_clock_sensor_label = nullptr;
    g_flip_clock_sensor_bold_label = nullptr;
    g_flip_clock_sensor_bold_y_label = nullptr;
    g_flip_clock_humidity_label = nullptr;
    g_flip_clock_humidity_bold_label = nullptr;
    g_flip_clock_humidity_bold_y_label = nullptr;
    g_flip_clock_temp_mood_canvas = nullptr;
    g_flip_clock_humi_mood_canvas = nullptr;
    g_flip_clock_temp_trend_canvas = nullptr;
    g_flip_clock_humi_trend_canvas = nullptr;
    g_flip_clock_day_label = nullptr;
    g_flip_clock_day_bold_label = nullptr;
    g_flip_clock_day_bold_y_label = nullptr;
    g_flip_clock_lunar_label = nullptr;
    g_flip_clock_lunar_bold_x_label = nullptr;
    g_flip_clock_lunar_bold_y_label = nullptr;
    g_flip_clock_lunar_bold_xy_label = nullptr;
    g_flip_clock_day_progress_canvas = nullptr;
    g_flip_clock_second_progress_canvas = nullptr;
    clear_pointer_array(g_battery_segments);
    clear_pointer_array(g_history_battery_segments);
    clear_pointer_array(g_gallery_battery_segments);
    clear_pointer_array(g_calendar_battery_segments);
    clear_pointer_array(g_weather_board_battery_segments);
    clear_pointer_array(g_flip_clock_battery_segments);
    g_history_chart_canvas = nullptr;
    g_history_temp_max_label = nullptr;
    g_history_temp_min_label = nullptr;
    g_history_humi_max_label = nullptr;
    g_history_humi_min_label = nullptr;
    clear_pointer_array(g_history_time_labels);
    clear_pointer_array(g_history_temp_axis_labels);
    clear_pointer_array(g_history_humi_axis_labels);
    clear_pointer_array(g_lower_panel_objects);
    clear_pointer_array(g_setup_status_labels);
    g_last_ui_second = -1;
    g_last_ui_minute = -1;
    g_last_ui_date_key = -1;
    g_last_ui_date_page = -1;
    g_last_day_progress_filled = -1;
    g_last_second_progress_filled = -1;
    g_last_status_gif_frame = -1;
    g_last_flip_clock_hour = -1;
    g_last_flip_clock_minute = -1;
    g_last_flip_clock_second = -1;
    g_last_flip_temp_mood = -1;
    g_last_flip_humi_mood = -1;
    g_last_flip_temp_trend = 99;
    g_last_flip_humi_trend = 99;
    g_last_flip_date_key = -1;
    g_last_flip_day_progress_filled = -1;
    g_last_flip_second_progress_filled = -1;
    g_last_flip_sensor_minute = -1;
    g_last_temp_trend_drawn = 99;
    g_last_humi_trend_drawn = 99;
    g_last_history_drawn_version = (uint32_t)-1;
    g_last_history_drawn_hour = -1;
}

void clear_info_object_refs()
{
    g_info_root = nullptr;
    g_network_diag_root = nullptr;
    g_settings_root = nullptr;
    clear_pointer_array(g_info_labels);
    clear_pointer_array(g_network_diag_labels);
    g_network_diag_summary_label = nullptr;
    g_network_diag_hint_label = nullptr;
    g_info_ota_label = nullptr;
    g_info_ota_hint_label = nullptr;
    g_info_ota_bar_frame = nullptr;
    g_info_ota_bar_fill = nullptr;
    clear_pointer_array(g_settings_labels);
    clear_pointer_array(g_settings_switch_dots);
    clear_pointer_array(g_settings_switch_texts);
    g_settings_feedback_label = nullptr;
    g_settings_ota_status_label = nullptr;
    g_settings_ota_hint_label = nullptr;
    g_settings_ota_bar_frame = nullptr;
    g_settings_ota_bar_fill = nullptr;
}

void remember_lower_panel_object(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    for (lv_obj_t *&slot : g_lower_panel_objects) {
        if (!slot) {
            slot = obj;
            return;
        }
    }
}

void set_obj_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_lower_panel_visible(bool visible)
{
    for (lv_obj_t *obj : g_lower_panel_objects) {
        set_obj_visible(obj, visible);
    }
}

void set_setup_panel_visible(bool visible)
{
    for (lv_obj_t *label : g_setup_status_labels) {
        set_obj_visible(label, visible);
    }
}

bool update_low_battery_state()
{
    bool previous = g_low_battery_mode;
    if (g_battery_percent >= 0) {
        if (!g_low_battery_mode && g_battery_percent < kLowBatteryEnterPercent) {
            g_low_battery_mode = true;
        } else if (g_low_battery_mode && g_battery_percent >= kLowBatteryExitPercent) {
            g_low_battery_mode = false;
        }
    }
    return previous != g_low_battery_mode;
}

void apply_clock_mode_visibility(bool setup_active)
{
    bool low = g_low_battery_mode;
    set_obj_visible(g_second_canvas, !low);
    set_obj_visible(g_day_progress_canvas, !low);
    set_obj_visible(g_second_progress_canvas, !low);
    set_obj_visible(g_low_battery_icon_canvas, low);
    set_lower_panel_visible(!setup_active && !low);
    set_setup_panel_visible(setup_active && !low);
    set_obj_visible(g_panel_sep_a, !setup_active || low);
    set_obj_visible(g_panel_sep_b, !setup_active || low);
    if (low || setup_active) {
        set_obj_visible(g_alert_pill, false);
        set_obj_visible(g_chime_status_icon_canvas, false);
        set_obj_visible(g_wifi_status_icon_canvas, false);
    }
}

void update_alert_pill(bool show, int alert_index)
{
    WeatherAlertData alert = {};
    get_weather_snapshot(nullptr, &alert);
    bool visible = show &&
                   !g_low_battery_mode &&
                   alert.active &&
                   alert.count > 0;
    set_obj_visible(g_alert_pill, visible);
    update_top_status_icons(visible);
    if (visible) {
        if (alert_index < 0) {
            alert_index = 0;
        }
        alert_index %= alert.count;
        set_label_text_if_changed(g_alert_label, alert.titles[alert_index]);
    }
}

void update_top_status_icons(bool alert_visible)
{
    bool allow = !alert_visible && !g_low_battery_mode && !g_setup_portal_active;
    set_obj_visible(g_chime_status_icon_canvas, allow && (g_hourly_chime_enabled || g_hourly_chime_all_day));
    set_obj_visible(g_wifi_status_icon_canvas, allow && g_wifi_radio_on);
}
