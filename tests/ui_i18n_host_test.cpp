#include "ui_i18n.h"
#include "ui_language_internal.h"

#include <assert.h>
#include <string.h>

int main()
{
    ui_language_store(UiLanguage::Traditional);
    assert(strcmp(ui_i18n_text(UiTextId::LanguageTraditional), "繁體中文") == 0);
    assert(strcmp(ui_weather_text(0), "晴朗") == 0);
    assert(strcmp(ui_weekday_text(0), "星期日") == 0);

    ui_language_store(UiLanguage::Simplified);
    assert(strcmp(ui_weather_text(61), "下雨") == 0);
    assert(strcmp(ui_weekday_text(1), "星期一") == 0);

    ui_language_store(UiLanguage::English);
    assert(strcmp(ui_i18n_text(UiTextId::LanguageEnglish), "English") == 0);
    assert(strcmp(ui_weather_text(1), "P.cldy") == 0);
    assert(strcmp(ui_weather_text(95), "Storm") == 0);
    assert(strcmp(ui_weather_advice(0), "Clear skies. Use sun protection.") == 0);
    assert(strcmp(ui_weather_advice(100), "Weather data incomplete. Check updates.") == 0);
    assert(strcmp(ui_weekday_text(6), "Saturday") == 0);
    assert(strcmp(ui_air_quality_category(25), "Excellent") == 0);
    assert(strcmp(ui_wind_direction(45), "NE") == 0);
    assert(strcmp(ui_wind_direction(360), "N") == 0);
    assert(strcmp(ui_wind_direction(-1), "--") == 0);
    return 0;
}
