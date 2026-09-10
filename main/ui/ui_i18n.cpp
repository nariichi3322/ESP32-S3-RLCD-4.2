#include "ui_i18n.h"

#include "weather_types.h"

namespace {

// APP_UI_LOCALE is a numeric id supplied by main/CMakeLists.txt. Only the
// selected field survives preprocessing in a firmware image. Host builds
// retain all fields for the legacy runtime-switching tests.
#if defined(APP_UI_LOCALE)
struct TextCatalogEntry { const char *selected; };
#if APP_UI_LOCALE == 1
#define UI_CATALOG(traditional, simplified, english, japanese) { simplified }
#elif APP_UI_LOCALE == 2
#define UI_CATALOG(traditional, simplified, english, japanese) { english }
#elif APP_UI_LOCALE == 3
#define UI_CATALOG(traditional, simplified, english, japanese) { japanese }
#else
#define UI_CATALOG(traditional, simplified, english, japanese) { traditional }
#endif
#else
struct TextCatalogEntry {
    const char *traditional;
    const char *simplified;
    const char *english;
    const char *japanese;
};
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese }
#endif

constexpr TextCatalogEntry kCatalog[] = {
#include "generated/ui_catalog_entries.inc"
};

static_assert(sizeof(kCatalog) / sizeof(kCatalog[0]) ==
                  static_cast<unsigned>(UiTextId::Count),
              "UI text catalog must contain one entry for every stable id");

const char *catalog_text(UiTextId id)
{
    const unsigned index = static_cast<unsigned>(id);
    if (index >= static_cast<unsigned>(UiTextId::Count)) return "";
#if defined(APP_UI_LOCALE)
    return kCatalog[index].selected;
#else
    const auto &entry = kCatalog[index];
    return ui_language_text(entry.traditional,
                            entry.simplified,
                            entry.english,
                            entry.japanese);
#endif
}

WeatherIconKind weather_kind(int code)
{
    if (code == 0) return WeatherIconKind::kClear;
    if (code >= 1 && code <= 2) return WeatherIconKind::kPartlyCloudy;
    if (code == 3) return WeatherIconKind::kCloudy;
    if (code == 45 || code == 48) return WeatherIconKind::kFog;
    if (code >= 51 && code <= 57) return WeatherIconKind::kDrizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WeatherIconKind::kRain;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return WeatherIconKind::kSnow;
    if (code >= 95 && code <= 99) return WeatherIconKind::kThunderstorm;
    return WeatherIconKind::kUnknown;
}
} // namespace

const char *ui_text(UiTextId id) { return catalog_text(id); }
const char *ui_format(UiTextId id) { return catalog_text(id); }
const char *ui_i18n_text(UiTextId id) { return catalog_text(id); }

const char *ui_weather_text(int code)
{
    switch (weather_kind(code)) {
    case WeatherIconKind::kClear: return ui_text(UiTextId::WeatherClear);
    case WeatherIconKind::kPartlyCloudy: return ui_text(UiTextId::WeatherPartlyCloudy);
    case WeatherIconKind::kCloudy: return ui_text(UiTextId::WeatherCloudy);
    case WeatherIconKind::kFog: return ui_text(UiTextId::WeatherFog);
    case WeatherIconKind::kDrizzle: return ui_text(UiTextId::WeatherDrizzle);
    case WeatherIconKind::kRain: return ui_text(UiTextId::WeatherRain);
    case WeatherIconKind::kSnow: return ui_text(UiTextId::WeatherSnow);
    case WeatherIconKind::kThunderstorm: return ui_text(UiTextId::WeatherThunderstorm);
    default: return ui_text(UiTextId::WeatherUnknown);
    }
}

const char *ui_weather_advice(int code)
{
    switch (weather_kind(code)) {
    case WeatherIconKind::kRain:
    case WeatherIconKind::kDrizzle:
    case WeatherIconKind::kThunderstorm: return ui_text(UiTextId::WeatherAdviceRain);
    case WeatherIconKind::kSnow: return ui_text(UiTextId::WeatherAdviceSnow);
    case WeatherIconKind::kClear: return ui_text(UiTextId::WeatherAdviceClear);
    case WeatherIconKind::kFog: return ui_text(UiTextId::WeatherAdviceFog);
    default: return ui_text(UiTextId::WeatherAdviceUnknown);
    }
}

const char *ui_weekday_text(int weekday)
{
    if (weekday < 0 || weekday > 6) return ui_text(UiTextId::UiPlaceholder);
    return ui_text(static_cast<UiTextId>(static_cast<unsigned>(UiTextId::WeekdaySunday) + weekday));
}

const char *ui_air_quality_category(int aqi)
{
    UiTextId id = UiTextId::AirHazardous;
    if (aqi <= 50) id = UiTextId::AirExcellent;
    else if (aqi <= 100) id = UiTextId::AirGood;
    else if (aqi <= 150) id = UiTextId::AirSensitive;
    else if (aqi <= 200) id = UiTextId::AirUnhealthy;
    else if (aqi <= 300) id = UiTextId::AirVeryUnhealthy;
    return ui_text(id);
}

const char *ui_wind_direction(int degrees)
{
    static constexpr UiTextId kDirections[] = {
        UiTextId::WindNorth, UiTextId::WindNortheast, UiTextId::WindEast,
        UiTextId::WindSoutheast, UiTextId::WindSouth, UiTextId::WindSouthwest,
        UiTextId::WindWest, UiTextId::WindNorthwest};
    if (degrees < 0) return ui_text(UiTextId::UiPlaceholder);
    const int normalized = degrees % 360;
    return ui_text(kDirections[((normalized + 22) % 360) / 45]);
}

#undef UI_CATALOG
