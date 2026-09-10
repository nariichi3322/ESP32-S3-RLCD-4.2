// 声明天气时钟界面使用的 LVGL 字体资源。
#pragma once

#include "lvgl.h"
#include <stdint.h>

#if !defined(APP_UI_LOCALE) || APP_UI_LOCALE == 0 || APP_UI_LOCALE == 1 || APP_UI_LOCALE == 2
LV_FONT_DECLARE(zh_font_16);
#endif
#if !defined(APP_UI_LOCALE) || APP_UI_LOCALE == 0 || APP_UI_LOCALE == 1
LV_FONT_DECLARE(zh_flip_lunar_22);
LV_FONT_DECLARE(zh_pomodoro_title_24);
#endif
#if !defined(APP_UI_LOCALE) || APP_UI_LOCALE == 3
LV_FONT_DECLARE(lv_font_simsun_16_cjk);
LV_FONT_DECLARE(lv_font_ja_extra);
#endif
LV_FONT_DECLARE(weather_icons_36);

enum class UiFontRole : uint8_t {
    Body16,
    Metric16,
    Calendar22,
    Pomodoro24,
};

const lv_font_t *ui_font(UiFontRole role);
const lv_font_t *ui_font_for_text(const char *text,
                                  const lv_font_t *preferred);
