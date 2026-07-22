// 声明 UI 构建、刷新、绘图和设置页交互的公共接口。
#pragma once
#include "app_state.h"
#include "ui_bitmap.h"
#include "ui_boot_screen.h"
#include "ui_canvas_primitives.h"
#include "ui_dseg_clock.h"
#include "ui_history_chart.h"
#include "ui_inverted_clock_card.h"
#include "ui_object_refs.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_settings_feedback.h"
#include "ui_settings_navigation.h"
#include "ui_status_gif.h"
#include "ui_task_notify.h"
#include "ui_time_format.h"
#include "ui_widgets.h"
#include "ui_work_page_catalog.h"
#include "wifi_portal_state.h"
#include "wifi_radio_state.h"

struct UiStatusRefreshSnapshot;

inline bool wifi_radio_on_for_status_icon()
{
    return wifi_radio_on_load();
}
constexpr const char *kClockWeatherCityPlaceholder = "--";
constexpr const char *kClockWeatherInfoWaitingText = "等待数据";
constexpr const char *kClockWeatherInfoSyncingText = "天气同步中";
constexpr const char *kClockWeatherInfoMissingApiKeyText = "设置 API Key";
constexpr const char *kClockWeatherTempPlaceholder = "--℃";
constexpr const char *kClockWeatherHumidityPlaceholder = "--%";
constexpr const char *kClockWeatherUnknownIconCode = "999";
struct WorkPageStatusLabels {
    lv_obj_t *date;
    lv_obj_t *summary;
    lv_obj_t *time;
};
void build_work_page_status_bar(lv_obj_t *screen,
                                int page,
                                bool show_summary,
                                bool show_time);
WorkPageStatusLabels get_work_page_status_labels(int page);
bool update_work_page_status_time(lv_obj_t *label, const struct tm &local);
bool update_work_page_sensor_summary(lv_obj_t *label);
bool update_non_clock_work_page_sensor_status(int page);
bool update_weather_clock_sensor_status();
void style_work_page_sensor_summary(lv_obj_t *label);
bool update_work_page_status_icons(int page,
                                   const UiStatusRefreshSnapshot &status,
                                   bool low_battery_mode,
                                   bool setup_active);
void format_axis_hour(time_t value, char *out, size_t out_len);
int value_to_plot_y(float value, float min_value, float max_value, int y, int h);
bool collect_history_window(time_t end_hour, HourlySensorSample *out, int *out_count);
bool update_history_page(const struct tm &local);
void build_history_page();
bool update_gallery_page(const struct tm &local);
void build_gallery_page();
bool update_calendar_page(const struct tm &local);
void build_calendar_page();
bool update_weather_board_page(const struct tm &local);
void build_weather_board_page();
bool update_time_ui(const struct tm &local, bool clock_page_active, int active_work_page);
bool update_setup_clock_header_time_ui(const struct tm &local);
void handle_settings_action();
void ui_task(void *);
