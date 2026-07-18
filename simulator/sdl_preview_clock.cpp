// 实现 SDL 天气时钟主页的静态构建、状态切换和差分刷新。
#include "sdl_preview_clock.h"

#include <stdio.h>
#include <string.h>

#include "dseg_digits.h"
#include "sdl_preview_weather.h"
#include "sdl_preview_widgets.h"
#include "status_gif_60.h"
#include "ui_icons.h"

LV_FONT_DECLARE(qweather_icons_36);
LV_FONT_DECLARE(zh_font_16);

namespace {
using sdl_preview_widgets::draw_1bit_icon;
using sdl_preview_widgets::make_bar;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::make_label_with_font;
using sdl_preview_widgets::set_label_text_if_changed;
using sdl_preview_widgets::set_obj_black;

constexpr int kTimeCanvasW = 292;
constexpr int kTimeCanvasH = 92;
constexpr int kSecondCanvasW = 60;
constexpr int kSecondCanvasH = 40;

const DsegGlyph *find_dseg_glyph(const DsegFont &font, char ch)
{
    const char *pos = strchr(font.chars, ch);
    if (!pos) return nullptr;
    return &font.glyphs[pos - font.chars];
}

int draw_dseg_text(lv_obj_t *canvas,
                   const DsegFont &font,
                   const char *text,
                   int cursor_x,
                   int baseline_y)
{
    int x_cursor = cursor_x;
    for (const char *p = text; *p; ++p) {
        const DsegGlyph *glyph = find_dseg_glyph(font, *p);
        if (!glyph) continue;
        uint32_t bit = 0;
        for (int y = 0; y < glyph->height; ++y) {
            for (int x = 0; x < glyph->width; ++x, ++bit) {
                uint8_t byte = font.bitmap[glyph->bitmap_offset + bit / 8];
                if (byte & (0x80 >> (bit & 7))) {
                    lv_canvas_set_px_color(canvas,
                                           x_cursor + glyph->x_offset + x,
                                           baseline_y + glyph->y_offset + y,
                                           lv_color_black());
                }
            }
        }
        x_cursor += glyph->x_advance;
    }
    return x_cursor;
}
} // namespace

SdlPreviewClock::SdlPreviewClock(sdl_preview_work_status::Bar &work_status)
    : work_status_(work_status),
      time_canvas_pixels_(kTimeCanvasW * kTimeCanvasH),
      second_canvas_pixels_(kSecondCanvasW * kSecondCanvasH),
      status_gif_canvas_pixels_(STATUS_GIF_WIDTH * STATUS_GIF_HEIGHT),
      alert_icon_canvas_pixels_(WARNING_ICON_WIDTH * WARNING_ICON_HEIGHT),
      low_battery_icon_canvas_pixels_(LOW_BATTERY_ICON_WIDTH * LOW_BATTERY_ICON_HEIGHT),
      temp_trend_canvas_pixels_(TREND_ICON_WIDTH * TREND_ICON_HEIGHT),
      humi_trend_canvas_pixels_(TREND_ICON_WIDTH * TREND_ICON_HEIGHT),
      temp_icon_canvas_pixels_(TEMP_ICON_WIDTH * TEMP_ICON_HEIGHT),
      humi_icon_canvas_pixels_(HUMI_ICON_WIDTH * HUMI_ICON_HEIGHT)
{
}

void SdlPreviewClock::update_trend_icon(lv_obj_t *canvas, int trend)
{
    const uint8_t *bits = nullptr;
    if (trend > 0) {
        bits = trend_up_icon_bits;
    } else if (trend < 0) {
        bits = trend_down_icon_bits;
    }
    if (bits) {
        draw_1bit_icon(canvas,
                       TREND_ICON_WIDTH,
                       TREND_ICON_HEIGHT,
                       TREND_ICON_BYTES_PER_ROW,
                       bits,
                       lv_color_black(),
                       lv_color_white());
    } else if (canvas) {
        lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
        lv_obj_invalidate(canvas);
    }
}

void SdlPreviewClock::draw_time(const struct tm &local)
{
    if (!time_canvas_) return;
    lv_canvas_fill_bg(time_canvas_, lv_color_white(), LV_OPA_COVER);
    char hm[6];
    snprintf(hm, sizeof(hm), "%02d:%02d", local.tm_hour, local.tm_min);
    draw_dseg_text(time_canvas_, kDSEG84Font, hm, 0, 88);
    lv_obj_invalidate(time_canvas_);
}

void SdlPreviewClock::draw_second(const struct tm &local)
{
    if (!second_canvas_) return;
    lv_canvas_fill_bg(second_canvas_, lv_color_white(), LV_OPA_COVER);
    char ss[3];
    snprintf(ss, sizeof(ss), "%02d", local.tm_sec);
    draw_dseg_text(second_canvas_, kDSEG36Font, ss, 0, 40);
    lv_obj_invalidate(second_canvas_);
}

void SdlPreviewClock::draw_status_gif_frame(int frame)
{
    if (!status_gif_canvas_) return;
    if (frame < 0) {
        frame = 0;
    } else if (frame >= STATUS_GIF_FRAME_COUNT) {
        frame = STATUS_GIF_FRAME_COUNT - 1;
    }
    const uint8_t *pixels = status_gif_frames[frame];
    const uint8_t *previous =
        last_status_gif_frame_ >= 0 ? status_gif_frames[last_status_gif_frame_] : nullptr;
    uint32_t bit = 0;
    bool changed = false;
    for (int y = 0; y < STATUS_GIF_HEIGHT; ++y) {
        for (int x = 0; x < STATUS_GIF_WIDTH; ++x, ++bit) {
            bool black = pixels[bit / 8] & (0x80 >> (bit & 7));
            if (previous) {
                bool previous_black = previous[bit / 8] & (0x80 >> (bit & 7));
                if (black == previous_black) {
                    continue;
                }
            }
            lv_canvas_set_px_color(status_gif_canvas_,
                                   x,
                                   y,
                                   black ? lv_color_black() : lv_color_white());
            changed = true;
        }
    }
    if (changed || last_status_gif_frame_ != frame) {
        lv_obj_invalidate(status_gif_canvas_);
    }
    last_status_gif_frame_ = frame;
}

void SdlPreviewClock::remember_lower_panel_object(lv_obj_t *obj)
{
    for (lv_obj_t *&slot : lower_panel_objects_) {
        if (!slot) {
            slot = obj;
            return;
        }
    }
}

void SdlPreviewClock::set_lower_panel_visible(bool visible)
{
    for (lv_obj_t *obj : lower_panel_objects_) {
        if (!obj) continue;
        if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void SdlPreviewClock::set_setup_panel_visible(bool visible)
{
    for (lv_obj_t *label : setup_status_labels_) {
        if (!label) continue;
        if (visible) lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void SdlPreviewClock::set_object_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void SdlPreviewClock::prepare_screen(lv_obj_t *screen)
{
    last_status_gif_frame_ = -1;
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void SdlPreviewClock::build_status_header(lv_obj_t *screen)
{
    struct tm local = {};
    work_status_.build(screen, local, false, false, -1);

    alert_pill_ = lv_obj_create(screen);
    lv_obj_clear_flag(alert_pill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(alert_pill_, 64, 11);
    lv_obj_set_size(alert_pill_, 128, 26);
    lv_obj_set_style_bg_color(alert_pill_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alert_pill_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(alert_pill_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(alert_pill_, 13, LV_PART_MAIN);
    lv_obj_set_style_pad_all(alert_pill_, 0, LV_PART_MAIN);
    lv_obj_add_flag(alert_pill_, LV_OBJ_FLAG_HIDDEN);

    alert_icon_canvas_ = lv_canvas_create(alert_pill_);
    lv_obj_clear_flag(alert_icon_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(alert_icon_canvas_, 4, 4);
    lv_obj_set_size(alert_icon_canvas_, WARNING_ICON_WIDTH, WARNING_ICON_HEIGHT);
    lv_obj_set_style_border_width(alert_icon_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(alert_icon_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(alert_icon_canvas_,
                         alert_icon_canvas_pixels_.data(),
                         WARNING_ICON_WIDTH,
                         WARNING_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(alert_icon_canvas_,
                   WARNING_ICON_WIDTH,
                   WARNING_ICON_HEIGHT,
                   WARNING_ICON_BYTES_PER_ROW,
                   warning_icon_bits,
                   lv_color_white(),
                   lv_color_black());
    alert_label_ = make_label_with_font(alert_pill_, 24, 4, 94, 18, "大风蓝色预警", &zh_font_16);
    lv_obj_set_style_text_color(alert_label_, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(alert_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(alert_label_, LV_LABEL_LONG_CLIP);
}

void SdlPreviewClock::build_weather_summary(lv_obj_t *screen)
{
    weather_city_label_ = make_label(screen, 14, 196, 76, 20, "--");
    remember_lower_panel_object(weather_city_label_);
    lv_obj_set_style_text_align(weather_city_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    weather_icon_label_ = make_label(screen, 91, 194, 34, 38, "");
    remember_lower_panel_object(weather_icon_label_);
    lv_obj_set_style_text_font(weather_icon_label_, &qweather_icons_36, LV_PART_MAIN);
    lv_obj_set_style_border_width(weather_icon_label_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weather_icon_label_, 0, LV_PART_MAIN);
    lv_obj_set_style_text_align(weather_icon_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    weather_info_label_ = make_label(screen, 14, 218, 76, 20, "等待数据");
    remember_lower_panel_object(weather_info_label_);
    lv_label_set_long_mode(weather_info_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(weather_info_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    weather_temp_label_ = make_label(screen, 20, 242, 68, 20, "--℃");
    weather_humi_label_ = make_label(screen, 20, 264, 68, 20, "--%");
    remember_lower_panel_object(weather_temp_label_);
    remember_lower_panel_object(weather_humi_label_);
    lv_obj_set_style_text_align(weather_temp_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(weather_humi_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

void SdlPreviewClock::build_sensor_summary(lv_obj_t *screen)
{
    temp_icon_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(temp_icon_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(temp_icon_canvas_, 153, 215);
    lv_obj_set_size(temp_icon_canvas_, TEMP_ICON_WIDTH, TEMP_ICON_HEIGHT);
    lv_obj_set_style_border_width(temp_icon_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(temp_icon_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(temp_icon_canvas_,
                         temp_icon_canvas_pixels_.data(),
                         TEMP_ICON_WIDTH,
                         TEMP_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(temp_icon_canvas_,
                   TEMP_ICON_WIDTH,
                   TEMP_ICON_HEIGHT,
                   TEMP_ICON_BYTES_PER_ROW,
                   temp_icon_bits,
                   lv_color_black(),
                   lv_color_white());
    humi_icon_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(humi_icon_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(humi_icon_canvas_, 151, 245);
    lv_obj_set_size(humi_icon_canvas_, HUMI_ICON_WIDTH, HUMI_ICON_HEIGHT);
    lv_obj_set_style_border_width(humi_icon_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(humi_icon_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(humi_icon_canvas_,
                         humi_icon_canvas_pixels_.data(),
                         HUMI_ICON_WIDTH,
                         HUMI_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(humi_icon_canvas_,
                   HUMI_ICON_WIDTH,
                   HUMI_ICON_HEIGHT,
                   HUMI_ICON_BYTES_PER_ROW,
                   humi_icon_bits,
                   lv_color_black(),
                   lv_color_white());
    temp_label_ = make_label(screen, 174, 214, 62, 28, "--.-℃");
    humi_label_ = make_label(screen, 174, 246, 62, 28, "--.-%");
    remember_lower_panel_object(temp_icon_canvas_);
    remember_lower_panel_object(humi_icon_canvas_);
    remember_lower_panel_object(temp_label_);
    remember_lower_panel_object(humi_label_);
    lv_obj_set_style_text_align(temp_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(humi_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    temp_trend_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(temp_trend_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(temp_trend_canvas_, 239, 215);
    lv_obj_set_size(temp_trend_canvas_, TREND_ICON_WIDTH, TREND_ICON_HEIGHT);
    lv_obj_set_style_border_width(temp_trend_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(temp_trend_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(temp_trend_canvas_,
                         temp_trend_canvas_pixels_.data(),
                         TREND_ICON_WIDTH,
                         TREND_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    update_trend_icon(temp_trend_canvas_, 1);
    humi_trend_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(humi_trend_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(humi_trend_canvas_, 239, 248);
    lv_obj_set_size(humi_trend_canvas_, TREND_ICON_WIDTH, TREND_ICON_HEIGHT);
    lv_obj_set_style_border_width(humi_trend_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(humi_trend_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(humi_trend_canvas_,
                         humi_trend_canvas_pixels_.data(),
                         TREND_ICON_WIDTH,
                         TREND_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    update_trend_icon(humi_trend_canvas_, -1);
    remember_lower_panel_object(temp_trend_canvas_);
    remember_lower_panel_object(humi_trend_canvas_);
}

void SdlPreviewClock::build_time_and_progress(lv_obj_t *screen)
{
    time_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(time_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(time_canvas_, 18, 76);
    lv_obj_set_size(time_canvas_, kTimeCanvasW, kTimeCanvasH);
    lv_obj_set_style_border_width(time_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(time_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(time_canvas_,
                         time_canvas_pixels_.data(),
                         kTimeCanvasW,
                         kTimeCanvasH,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(time_canvas_, lv_color_white(), LV_OPA_COVER);

    second_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(second_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(second_canvas_, 320, 124);
    lv_obj_set_size(second_canvas_, kSecondCanvasW, kSecondCanvasH);
    lv_obj_set_style_border_width(second_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(second_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(second_canvas_,
                         second_canvas_pixels_.data(),
                         kSecondCanvasW,
                         kSecondCanvasH,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(second_canvas_, lv_color_white(), LV_OPA_COVER);

    status_gif_canvas_ = lv_canvas_create(screen);
    remember_lower_panel_object(status_gif_canvas_);
    lv_obj_clear_flag(status_gif_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(status_gif_canvas_, 279, 196);
    lv_obj_set_size(status_gif_canvas_, STATUS_GIF_WIDTH, STATUS_GIF_HEIGHT);
    lv_obj_set_style_border_width(status_gif_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_gif_canvas_, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(status_gif_canvas_,
                         status_gif_canvas_pixels_.data(),
                         STATUS_GIF_WIDTH,
                         STATUS_GIF_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(status_gif_canvas_, lv_color_white(), LV_OPA_COVER);
    draw_status_gif_frame(0);

    lv_obj_t *top_line = make_bar(screen, 18, 54, 364, 4);
    lv_obj_t *bottom_line = make_bar(screen, 18, 184, 364, 4);
    day_progress_.build(screen, 59);
    second_progress_.build(screen, 180);
    panel_sep_a_ = make_bar(screen, 139, 188, 2, 102);
    panel_sep_b_ = make_bar(screen, 260, 188, 2, 102);
    remember_lower_panel_object(panel_sep_a_);
    remember_lower_panel_object(panel_sep_b_);
    set_obj_black(top_line, true);
    set_obj_black(bottom_line, true);
    set_obj_black(panel_sep_a_, true);
    set_obj_black(panel_sep_b_, true);

    low_battery_icon_canvas_ = lv_canvas_create(screen);
    lv_obj_clear_flag(low_battery_icon_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(low_battery_icon_canvas_, 156, 214);
    lv_obj_set_size(low_battery_icon_canvas_, LOW_BATTERY_ICON_WIDTH, LOW_BATTERY_ICON_HEIGHT);
    lv_obj_set_style_border_width(low_battery_icon_canvas_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(low_battery_icon_canvas_, 0, LV_PART_MAIN);
    lv_obj_add_flag(low_battery_icon_canvas_, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_buffer(low_battery_icon_canvas_,
                         low_battery_icon_canvas_pixels_.data(),
                         LOW_BATTERY_ICON_WIDTH,
                         LOW_BATTERY_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(low_battery_icon_canvas_,
                   LOW_BATTERY_ICON_WIDTH,
                   LOW_BATTERY_ICON_HEIGHT,
                   LOW_BATTERY_ICON_BYTES_PER_ROW,
                   low_battery_icon_bits,
                   lv_color_black(),
                   lv_color_white());
}

void SdlPreviewClock::build_setup_status(lv_obj_t *screen)
{
    static const int setup_y[] = {194, 212, 230, 248, 266, 284};
    static const char *setup_text[] = {
        "Setup Mode",
        "AP SSID: WeatherClock-ABCD",
        "AP Password: 12345678",
        "Portal IP: 192.168.4.1",
        "STA SSID: HomeWiFi",
        "STA IP: --",
    };
    for (int i = 0; i < 6; ++i) {
        setup_status_labels_[i] =
            make_label_with_font(screen, 26, setup_y[i], 348, 18, setup_text[i], &lv_font_montserrat_14);
        lv_obj_add_flag(setup_status_labels_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void SdlPreviewClock::build(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    prepare_screen(screen);
    build_status_header(screen);
    build_weather_summary(screen);
    build_sensor_summary(screen);
    build_time_and_progress(screen);
    build_setup_status(screen);
}

void SdlPreviewClock::populate_sample_data()
{
    set_label_text_if_changed(temp_label_, "24.6℃");
    set_label_text_if_changed(humi_label_, "58.0%");
    set_label_text_if_changed(weather_city_label_, "杭州");
    set_label_text_if_changed(weather_info_label_, "晴");
    set_label_text_if_changed(weather_temp_label_, "26℃");
    set_label_text_if_changed(weather_humi_label_, "58%");
    set_label_text_if_changed(weather_icon_label_, preview_weather_icon_text("100"));
    work_status_.update_battery(76);
}

void SdlPreviewClock::apply_low_battery(bool low)
{
    set_object_visible(second_canvas_, !low);
    set_object_visible(day_progress_.object(), !low);
    set_object_visible(second_progress_.object(), !low);
    set_lower_panel_visible(!low);
    set_setup_panel_visible(false);
    set_object_visible(panel_sep_a_, true);
    set_object_visible(panel_sep_b_, true);
    set_object_visible(low_battery_icon_canvas_, low);
    set_object_visible(alert_pill_, false);
    work_status_.set_status_icons_visible(!low);
}

void SdlPreviewClock::apply_alert(bool visible)
{
    set_object_visible(alert_pill_, visible);
    work_status_.set_status_icons_visible(!visible);
    if (visible) {
        set_label_text_if_changed(alert_label_, "大风蓝色预警");
    }
}

void SdlPreviewClock::show_setup_status()
{
    set_lower_panel_visible(false);
    set_setup_panel_visible(true);
    work_status_.set_status_icons_visible(false);
}

void SdlPreviewClock::update_time(const struct tm &local)
{
    int minute_key = local.tm_hour * 60 + local.tm_min;
    if (minute_key != last_minute_) {
        last_minute_ = minute_key;
        draw_time(local);
        int day_seconds = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
        int day_filled = (day_seconds * 60) / (24 * 3600);
        day_progress_.update(day_filled);
    }
    if (local.tm_sec != last_second_) {
        last_second_ = local.tm_sec;
        draw_second(local);
        draw_status_gif_frame(local.tm_sec % STATUS_GIF_FRAME_COUNT);
        second_progress_.update(local.tm_sec + 1);
    }

    work_status_.update_date(local);
}

void SdlPreviewClock::update_battery(int percent)
{
    work_status_.update_battery(percent);
}
