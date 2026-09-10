// 以與韌體相同的版面比例呈現聚合時鐘，供 SDL 預覽與截圖使用。
#include "sdl_preview_aggregate.h"

#include "sdl_preview_widgets.h"
#include "ui_work_page_layout.h"

LV_FONT_DECLARE(zh_font_16);
LV_FONT_DECLARE(weather_icons_36);

namespace {
using sdl_preview_widgets::make_black_bar;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::make_label_with_font;

void make_panel(lv_obj_t *root, int x, int y, int width, int height, bool black)
{
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, black ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *centered_label(lv_obj_t *root,
                         int x,
                         int y,
                         int width,
                         int height,
                         const char *text,
                         const lv_font_t *font)
{
    lv_obj_t *label = make_label_with_font(root, x, y, width, height, text, font);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}
} // namespace

void build_aggregate_clock_preview_body(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }

    make_panel(screen, 18, 66, 364, 100, true);
    centered_label(screen, 24, 76, 104, 80, "09", &lv_font_montserrat_48);
    centered_label(screen, 148, 76, 104, 80, ":", &lv_font_montserrat_48);
    centered_label(screen, 272, 76, 104, 80, "42", &lv_font_montserrat_48);

    make_panel(screen, 18, 174, 222, 120, false);
    lv_obj_t *weather_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(weather_panel);
    lv_obj_set_pos(weather_panel, 18, 174);
    lv_obj_set_size(weather_panel, 222, 28);
    lv_obj_set_style_bg_color(weather_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(weather_panel, LV_OPA_COVER, LV_PART_MAIN);
    centered_label(screen, 26, 180, 126, 20, "台北", &zh_font_16);
    centered_label(screen, 160, 180, 72, 20, "今日天氣", &zh_font_16);
    lv_obj_t *icon = centered_label(screen, 31, 207, 48, 42, "O", &weather_icons_36);
    lv_obj_set_style_text_color(icon, lv_color_black(), LV_PART_MAIN);
    centered_label(screen, 28, 250, 88, 20, "晴", &zh_font_16);
    centered_label(screen, 108, 202, 128, 54, "26°C", &lv_font_montserrat_48);
    centered_label(screen, 26, 274, 208, 17, "最高 29°C  最低 22°C", &lv_font_montserrat_12);

    make_panel(screen, 248, 174, 134, 58, false);
    centered_label(screen, 251, 176, 74, 56, "10", &lv_font_montserrat_48);
    centered_label(screen, 325, 178, 54, 26, "八月", &zh_font_16);
    centered_label(screen, 325, 204, 54, 27, "初五", &zh_font_16);

    make_panel(screen, 248, 238, 134, 56, true);
    centered_label(screen, 255, 241, 120, 22, "室內 25.3°C", &lv_font_montserrat_12);
    centered_label(screen, 255, 266, 120, 22, "濕度 58%", &lv_font_montserrat_12);
}
