// 實作 SDL 天氣看板與供應商中立的單色天氣符號。
#include "sdl_preview_weather.h"

#include <string.h>

#include "core/app_constexpr.h"
#include "sdl_preview_widgets.h"
#include "ui_weather_board_layout.h"

LV_FONT_DECLARE(zh_font_16);
LV_FONT_DECLARE(weather_icons_36);

namespace {
using namespace ui_weather_board_layout;

using sdl_preview_widgets::make_black_bar;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::make_label_with_font;

void style_weather_preview_card(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}
} // namespace

const char *preview_weather_icon_text(const char *kind)
{
    if (!kind) return "?";
    if (strcmp(kind, "clear") == 0) return "O";
    if (strcmp(kind, "partly") == 0) return "o";
    if (strcmp(kind, "cloud") == 0) return "=";
    if (strcmp(kind, "rain") == 0) return "/";
    if (strcmp(kind, "snow") == 0) return "*";
    if (strcmp(kind, "storm") == 0) return "!";
    return "?";
}

void build_weather_board_preview_body(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }

    make_label(screen,
               kWeatherBoardCurrentCityX,
               kWeatherBoardCurrentCityY,
               kWeatherBoardCurrentCityW,
               kWeatherBoardCurrentCityH,
               "杭州");
    make_label_with_font(screen,
                         kWeatherBoardCurrentTempX,
                         kWeatherBoardCurrentTempY,
                         kWeatherBoardCurrentTempW,
                         kWeatherBoardCurrentTempH,
                         "26",
                         &lv_font_montserrat_48);
    make_label_with_font(screen,
                         kWeatherBoardCurrentUnitX,
                         kWeatherBoardCurrentUnitY,
                         kWeatherBoardCurrentUnitW,
                         kWeatherBoardCurrentUnitH,
                         "°C",
                         &lv_font_montserrat_24);
    lv_obj_t *icon = make_label(screen,
                                kWeatherBoardCurrentIconX,
                                kWeatherBoardCurrentIconY,
                                kWeatherBoardCurrentIconW,
                                kWeatherBoardCurrentIconH,
                                preview_weather_icon_text("clear"));
    lv_obj_set_style_text_font(icon, &weather_icons_36, LV_PART_MAIN);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    make_label(screen,
               kWeatherBoardCurrentTextX,
               kWeatherBoardCurrentTextY,
               kWeatherBoardCurrentTextW,
               kWeatherBoardCurrentTextH,
               "晴");
    make_label(screen,
               kWeatherBoardTodayRangeX,
               kWeatherBoardTodayRangeY,
               kWeatherBoardTodayRangeW,
               kWeatherBoardTodayRangeH,
               "今日 22/29°C");

    static constexpr const char *kDays[] = {
        "周二\n23日", "周三\n24日", "周四\n25日", "周五\n26日", "周六\n27日", "周日\n28日",
    };
    static constexpr const char *kTexts[] = {"晴", "多云转晴", "小到中雨", "阴", "雨夹雪", "大到暴雨"};
    static constexpr const char *kIcons[] = {"clear", "partly", "rain", "cloud", "snow", "storm"};
    static constexpr const char *kRanges[] = {"22/29°C", "23/30°C", "-3/2°C", "22/28°C", "-8/-2°C", "18/24°C"};
    static_assert(array_count(kDays) == array_count(kTexts) &&
                      array_count(kDays) == array_count(kIcons) &&
                      array_count(kDays) == array_count(kRanges) &&
                      array_count(kDays) == array_count(kForecastCardX),
                  "weather preview forecast columns must stay aligned");
    for (size_t i = 0; i < array_count(kDays); ++i) {
        int x = kForecastCardX[i];
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_set_pos(card, x, kForecastCardY);
        lv_obj_set_size(card, kForecastCardW, kForecastCardH);
        style_weather_preview_card(card);
        lv_obj_t *date = make_label_with_font(screen,
                                              x,
                                              kForecastCardY,
                                              kForecastCardW,
                                              kForecastCardDateH,
                                              kDays[i],
                                              &zh_font_16);
        lv_label_set_long_mode(date, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *small_icon = make_label(screen,
                                          x,
                                          kForecastCardY + kForecastCardIconY,
                                          kForecastCardW,
                                          kForecastCardIconH,
                                          preview_weather_icon_text(kIcons[i]));
        lv_obj_set_style_text_font(small_icon, &weather_icons_36, LV_PART_MAIN);
        lv_obj_set_style_text_align(small_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *text = make_label_with_font(screen,
                                              x,
                                              kForecastCardY + kForecastCardTextY,
                                              kForecastCardW,
                                              kForecastCardTextH,
                                              kTexts[i],
                                              &zh_font_16);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *range = make_label_with_font(screen,
                                               x,
                                               kForecastCardY + kForecastCardRangeY,
                                               kForecastCardW,
                                               kForecastCardRangeH,
                                               kRanges[i],
                                               &lv_font_montserrat_12);
        lv_obj_set_style_text_align(range, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    make_black_bar(screen,
                   kWeatherBoardDetailLineX,
                   kWeatherBoardDetailLineY,
                   kWeatherBoardDetailLineW,
                   kWeatherBoardDetailLineH);
    make_label(screen,
               kWeatherBoardLeftColumnX,
               kWeatherBoardDetailTopY,
               kWeatherBoardAirLabelW,
               kWeatherBoardDetailLabelH,
               "AQI 42 优");
    make_label(screen,
               kWeatherBoardMiddleColumnX,
               kWeatherBoardDetailTopY,
               kWeatherBoardHumidityLabelW,
               kWeatherBoardDetailLabelH,
               "湿度 58%");
    make_label(screen,
               kWeatherBoardRightColumnX,
               kWeatherBoardDetailTopY,
               kWeatherBoardWindLabelW,
               kWeatherBoardDetailLabelH,
               "东北风 3级");
    make_label(screen,
               kWeatherBoardLeftColumnX,
               kWeatherBoardDetailBottomY,
               kWeatherBoardSunriseLabelW,
               kWeatherBoardSunLabelH,
               "日出 05:01");
    make_label(screen,
               kWeatherBoardMiddleColumnX,
               kWeatherBoardDetailBottomY,
               kWeatherBoardSunsetLabelW,
               kWeatherBoardSunLabelH,
               "日落 19:06");
    make_label(screen,
               kWeatherBoardRightColumnX,
               kWeatherBoardDetailBottomY,
               kWeatherBoardSunCountdownLabelW,
               kWeatherBoardSunLabelH,
               "距日落 01:05");
    lv_obj_t *advice = make_label(screen,
                                  kWeatherBoardAdviceX,
                                  kWeatherBoardAdviceY,
                                  kWeatherBoardAdviceW,
                                  kWeatherBoardAdviceH,
                                  "天气平稳，适合轻装出行。");
    lv_label_set_long_mode(advice, LV_LABEL_LONG_WRAP);
}
