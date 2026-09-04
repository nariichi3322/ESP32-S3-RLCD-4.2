#include "ui_i18n.h"

#include "weather_types.h"

namespace {
struct TextTriplet { const char *traditional; const char *simplified; const char *english; };

constexpr TextTriplet kTexts[] = {
    {"繁體中文", "繁体中文", "Traditional Chinese"},
    {"簡體中文", "简体中文", "Simplified Chinese"},
    {"英文", "英文", "English"},
    {"晴朗", "晴朗", "Clear"},
    {"多雲", "多云", "P.cldy"},
    {"陰", "阴", "Cloudy"},
    {"霧", "雾", "Fog"},
    {"毛毛雨", "毛毛雨", "Drizzle"},
    {"下雨", "下雨", "Rain"},
    {"下雪", "下雪", "Snow"},
    {"雷雨", "雷雨", "Storm"},
    {"未知", "未知", "Unk."},
    {"有雨雪，出門記得帶傘。", "有雨雪，出门记得带伞。", "Rain/snow expected. Take an umbrella."},
    {"天氣寒冷，請注意保暖與路面結冰。", "天气寒冷，请注意保暖与路面结冰。", "Cold weather. Dress warmly; watch for ice."},
    {"天氣晴朗，戶外活動請注意防曬。", "天气晴朗，户外活动请注意防晒。", "Clear skies. Use sun protection."},
    {"能見度較低，行車請減速並保持距離。", "能见度较低，行车请减速并保持距离。", "Low visibility. Drive carefully."},
    {"天氣資料不足，請留意最新資訊。", "天气数据不足，请留意最新信息。", "Weather data incomplete. Check updates."},
    {"星期日", "星期日", "Sunday"}, {"星期一", "星期一", "Monday"},
    {"星期二", "星期二", "Tuesday"}, {"星期三", "星期三", "Wednesday"},
    {"星期四", "星期四", "Thursday"}, {"星期五", "星期五", "Friday"},
    {"星期六", "星期六", "Saturday"},
    {"優", "优", "Excellent"}, {"良", "良", "Good"},
    // The board's compact AQI column cannot display the full standard label.
    {"敏感不健康", "敏感不健康", "Sensitive"},
    {"不健康", "不健康", "Unhealthy"}, {"非常不健康", "非常不健康", "Very bad"},
    {"危害", "危害", "Hazardous"},
    {"北", "北", "N"}, {"東北", "东北", "NE"}, {"東", "东", "E"},
    {"東南", "东南", "SE"}, {"南", "南", "S"}, {"西南", "西南", "SW"},
    {"西", "西", "W"}, {"西北", "西北", "NW"},
};

static_assert(sizeof(kTexts) / sizeof(kTexts[0]) == static_cast<unsigned>(UiTextId::Count),
              "i18n text table must contain one row for every UiTextId");

const char *select(const TextTriplet &text)
{
    return ui_language_text(text.traditional, text.simplified, text.english);
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
}

const char *ui_i18n_text(UiTextId id)
{
    const auto index = static_cast<unsigned>(id);
    return index < static_cast<unsigned>(UiTextId::Count) ? select(kTexts[index]) : "";
}

const char *ui_weather_text(int code)
{
    switch (weather_kind(code)) {
    case WeatherIconKind::kClear: return ui_i18n_text(UiTextId::WeatherClear);
    case WeatherIconKind::kPartlyCloudy: return ui_i18n_text(UiTextId::WeatherPartlyCloudy);
    case WeatherIconKind::kCloudy: return ui_i18n_text(UiTextId::WeatherCloudy);
    case WeatherIconKind::kFog: return ui_i18n_text(UiTextId::WeatherFog);
    case WeatherIconKind::kDrizzle: return ui_i18n_text(UiTextId::WeatherDrizzle);
    case WeatherIconKind::kRain: return ui_i18n_text(UiTextId::WeatherRain);
    case WeatherIconKind::kSnow: return ui_i18n_text(UiTextId::WeatherSnow);
    case WeatherIconKind::kThunderstorm: return ui_i18n_text(UiTextId::WeatherThunderstorm);
    default: return ui_i18n_text(UiTextId::WeatherUnknown);
    }
}

const char *ui_weather_advice(int code)
{
    switch (weather_kind(code)) {
    case WeatherIconKind::kRain:
    case WeatherIconKind::kDrizzle:
    case WeatherIconKind::kThunderstorm: return ui_i18n_text(UiTextId::WeatherAdviceRain);
    case WeatherIconKind::kSnow: return ui_i18n_text(UiTextId::WeatherAdviceSnow);
    case WeatherIconKind::kClear: return ui_i18n_text(UiTextId::WeatherAdviceClear);
    case WeatherIconKind::kFog: return ui_i18n_text(UiTextId::WeatherAdviceFog);
    default: return ui_i18n_text(UiTextId::WeatherAdviceUnknown);
    }
}

const char *ui_weekday_text(int weekday)
{
    if (weekday < 0 || weekday > 6) return "--";
    return ui_i18n_text(static_cast<UiTextId>(static_cast<unsigned>(UiTextId::WeekdaySunday) + weekday));
}

const char *ui_air_quality_category(int aqi)
{
    UiTextId id = UiTextId::AirHazardous;
    if (aqi <= 50) id = UiTextId::AirExcellent;
    else if (aqi <= 100) id = UiTextId::AirGood;
    else if (aqi <= 150) id = UiTextId::AirSensitive;
    else if (aqi <= 200) id = UiTextId::AirUnhealthy;
    else if (aqi <= 300) id = UiTextId::AirVeryUnhealthy;
    return ui_i18n_text(id);
}

const char *ui_wind_direction(int degrees)
{
    static constexpr UiTextId kDirections[] = {
        UiTextId::WindNorth, UiTextId::WindNortheast, UiTextId::WindEast,
        UiTextId::WindSoutheast, UiTextId::WindSouth, UiTextId::WindSouthwest,
        UiTextId::WindWest, UiTextId::WindNorthwest};
    if (degrees < 0) return "--";
    const int normalized = degrees % 360;
    return ui_i18n_text(kDirections[((normalized + 22) % 360) / 45]);
}
