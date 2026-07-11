// 声明 UI 构建、刷新、绘图和设置页交互的公共接口。
#pragma once
#include "app_state.h"

void notify_ui_task();
inline bool wifi_connected_for_status_icon()
{
    return g_app_events && ((xEventGroupGetBits(g_app_events) & kWifiConnectedBit) != 0);
}
constexpr const char *kClockWeatherCityPlaceholder = "--";
constexpr const char *kClockWeatherInfoWaitingText = "等待数据";
constexpr const char *kClockWeatherInfoSyncingText = "天气同步中";
constexpr const char *kClockWeatherInfoMissingApiKeyText = "设置 API Key";
constexpr const char *kClockWeatherTempPlaceholder = "--℃";
constexpr const char *kClockWeatherHumidityPlaceholder = "--%";
constexpr const char *kClockWeatherUnknownIconCode = "999";
lv_color_t *alloc_canvas_buffer(int width, int height);
void set_obj_black(lv_obj_t *obj, bool active);
lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *make_black_bar(lv_obj_t *parent, int x, int y, int w, int h);
void draw_progress_segment(lv_obj_t *canvas, int index, bool filled);
void invalidate_progress_segment(lv_obj_t *canvas, int index);
void build_progress_canvas(lv_obj_t *parent, lv_obj_t **canvas, lv_color_t **buf, int y);
void update_progress_canvas(lv_obj_t *canvas, int filled, int *last_filled);
bool packed_1bit_bit_is_set(const uint8_t *bits, uint32_t bit_index);
void draw_1bit_icon(lv_obj_t *canvas,
                    int width,
                    int height,
                    int bytes_per_row,
                    const uint8_t *bits,
                    lv_color_t fg,
                    lv_color_t bg);
bool update_trend_icon(lv_obj_t *canvas, int trend, int *last_trend);
void build_work_page_status_bar(lv_obj_t *screen,
                                int page,
                                lv_obj_t **date_label,
                                lv_obj_t **summary_label,
                                lv_obj_t **time_label,
                                bool show_time);
bool update_work_page_status_time(lv_obj_t *label, const struct tm &local);
bool update_work_page_sensor_summary(lv_obj_t *label);
void style_work_page_sensor_summary(lv_obj_t *label);
bool update_work_page_status_icons(int page);
const DsegGlyph *find_dseg_glyph(const DsegFont &font, char ch);
int draw_dseg_text(lv_obj_t *canvas, const DsegFont &font, const char *text, int cursor_x, int baseline_y);
void draw_time_canvas(const struct tm &local);
void draw_second_canvas(const struct tm &local);
void draw_status_gif_frame(int frame);
lv_obj_t *make_label_with_font(lv_obj_t *parent, int x, int y, int w, int h, const char *text, const lv_font_t *font);
lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h, const char *text);
bool center_align_label(lv_obj_t *label);
lv_obj_t *make_centered_label(lv_obj_t *parent,
                              int x,
                              int y,
                              int w,
                              int h,
                              const char *text,
                              const char *warning);
lv_obj_t *make_centered_label_with_font(lv_obj_t *parent,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        const char *text,
                                        const lv_font_t *font,
                                        const char *warning);
bool set_label_text_if_changed(lv_obj_t *label, const char *text);
lv_obj_t *create_page_root();
void set_page_visible(lv_obj_t *page, bool visible);
void show_page(lv_obj_t *page);
lv_obj_t *active_work_page_root();
void show_active_work_page();
bool is_work_page_enabled(int page);
const char *work_page_name(int page);
int display_settings_item_work_page(int item);
int first_enabled_work_page();
int next_enabled_work_page(int current_page);
int first_enabled_work_page_order_index();
int next_enabled_work_page_order_index(int current_order_index);
int valid_enabled_work_page_order_index(int current_order_index);
void ensure_active_work_page_enabled();
void reset_work_page_order();
void normalize_work_page_order();
void format_time_or_dash(time_t value, char *out, size_t out_len);
void clear_clock_object_refs();
void clear_info_object_refs();
void remember_lower_panel_object(lv_obj_t *obj);
void set_lower_panel_visible(bool visible);
void set_setup_panel_visible(bool visible);
void set_obj_visible(lv_obj_t *obj, bool visible);
bool update_low_battery_state();
void apply_clock_mode_visibility(bool setup_active);
void update_alert_pill(bool show, int alert_index = 0);
void update_top_status_icons(bool alert_visible);
void draw_boot_anim_frame_index(int frame);
void boot_anim_task(void *);
void finish_boot_anim_to_last_frame();
void show_boot_screen();
void update_boot_screen(int percent, const char *status, const char *detail);
void finish_boot_screen();
void build_boot_info_page();
void update_boot_info_page();
void build_network_diag_page();
bool update_network_diag_page();
void style_settings_item(lv_obj_t *label, bool selected);
void build_settings_page();
bool update_settings_page();
void set_settings_feedback(const char *text, uint32_t duration_ms);
bool is_settings_sync_busy();
int settings_secondary_count(int primary);
void reset_settings_confirmation();
void handle_settings_key_short();
void handle_settings_key_long();
void begin_settings_sync(SettingsSyncOp op, const char *text);
void finish_settings_sync(SettingsSyncOp op, const char *text);
int clamp_int(int value, int min_value, int max_value);
void canvas_set_px_safe(lv_obj_t *canvas, int x, int y, int w, int h, lv_color_t color);
void canvas_draw_line(lv_obj_t *canvas, int w, int h, int x0, int y0, int x1, int y1, lv_color_t color);
void canvas_draw_dashed_hline(lv_obj_t *canvas, int w, int h, int x1, int x2, int y, lv_color_t color);
void canvas_draw_filled_circle(lv_obj_t *canvas, int w, int h, int cx, int cy, int radius, lv_color_t color);
void style_history_value_badge(lv_obj_t *label);
void format_axis_hour(time_t value, char *out, size_t out_len);
int value_to_plot_y(float value, float min_value, float max_value, int y, int h);
void set_history_badge(lv_obj_t *label,
                       const char *text,
                       int canvas_x,
                       int canvas_y,
                       int point_x,
                       int point_y,
                       int plot_x,
                       int plot_y,
                       int plot_w,
                       int plot_h);
bool collect_history_window(time_t end_hour, HourlySensorSample *out, int *out_count);
void update_history_axis_labels(time_t start, time_t end);
void draw_history_chart_panel(lv_obj_t *canvas,
                              int canvas_w,
                              int canvas_h,
                              const HourlySensorSample *samples,
                              int sample_count,
                              bool temperature,
                              int plot_x,
                              int plot_y,
                              int plot_w,
                              int plot_h,
                              lv_obj_t *max_label,
                              lv_obj_t *min_label,
                              lv_obj_t **axis_labels);
bool update_history_page(const struct tm &local);
void build_history_page();
bool update_gallery_page(const struct tm &local);
void build_gallery_page();
bool update_calendar_page(const struct tm &local);
void build_calendar_page();
bool update_weather_board_page(const struct tm &local);
void build_weather_board_page();
void build_inverted_clock_cards(lv_obj_t *parent,
                                lv_obj_t *card_canvas[3],
                                lv_color_t *card_canvas_buf[3]);
bool update_inverted_clock_cards(const struct tm &local,
                                 lv_obj_t *card_canvas[3],
                                 int last_values[3]);
bool update_flip_clock_page(const struct tm &local);
void build_flip_clock_page();
bool update_xiaozhi_page(const struct tm &local);
uint32_t xiaozhi_subtitle_animation_delay_ms();
void build_xiaozhi_page();
void build_clock_ui();
bool update_time_ui(const struct tm &local, bool clock_page_active, int active_work_page);
void handle_settings_action();
void ui_task(void *);
