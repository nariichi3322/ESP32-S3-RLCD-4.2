#pragma once

#include "ui_language.h"

// Stable IDs keep translations independent from the source-language spelling.
enum class UiTextId : uint16_t {
    LanguageTraditional,
    LanguageSimplified,
    LanguageEnglish,
    WeatherClear,
    WeatherPartlyCloudy,
    WeatherCloudy,
    WeatherFog,
    WeatherDrizzle,
    WeatherRain,
    WeatherSnow,
    WeatherThunderstorm,
    WeatherUnknown,
    WeatherAdviceRain,
    WeatherAdviceSnow,
    WeatherAdviceClear,
    WeatherAdviceFog,
    WeatherAdviceUnknown,
    WeekdaySunday,
    WeekdayMonday,
    WeekdayTuesday,
    WeekdayWednesday,
    WeekdayThursday,
    WeekdayFriday,
    WeekdaySaturday,
    AirExcellent,
    AirGood,
    AirSensitive,
    AirUnhealthy,
    AirVeryUnhealthy,
    AirHazardous,
    WindNorth,
    WindNortheast,
    WindEast,
    WindSoutheast,
    WindSouth,
    WindSouthwest,
    WindWest,
    WindNorthwest,
    Count,
};

const char *ui_i18n_text(UiTextId id);
const char *ui_weather_text(int weather_code);
const char *ui_weather_advice(int weather_code);
const char *ui_weekday_text(int weekday);
const char *ui_air_quality_category(int aqi);
const char *ui_wind_direction(int degrees);
