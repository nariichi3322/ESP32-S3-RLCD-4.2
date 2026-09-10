/*
 * Japanese image font adapter.
 *
 * LVGL ships a compact SimSun CJK subset containing ASCII, Hiragana,
 * Katakana, punctuation, and the common Kanji used by the calendar and UI.
 * Keep this translation unit in the Japanese image only; other locale images
 * never link the CJK catalog or font data.
 */
#if defined(APP_UI_LOCALE) && APP_UI_LOCALE == 3
#ifndef LV_FONT_SIMSUN_16_CJK
#define LV_FONT_SIMSUN_16_CJK 1
#endif
#include "../../managed_components/lvgl__lvgl/src/font/lv_font_simsun_16_cjk.c"
#endif
