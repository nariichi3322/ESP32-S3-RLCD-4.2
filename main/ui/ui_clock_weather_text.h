// 集中定义天气时钟面板的占位和同步状态文本。
#pragma once

#include "ui_i18n.h"

inline const char *clock_weather_city_placeholder()
{
    return ui_text(UiTextId::ClockWeatherCityPlaceholder);
}

inline const char *clock_weather_info_waiting_text()
{
    return ui_text(UiTextId::WeatherPanelWaiting);
}

inline const char *clock_weather_info_syncing_text()
{
    return ui_text(UiTextId::WeatherPanelSyncing);
}

inline const char *clock_weather_info_configuration_required_text()
{
    return ui_text(UiTextId::WeatherPanelConfigureWifi);
}

inline const char *clock_weather_temperature_placeholder()
{
    return ui_text(UiTextId::ClockWeatherTemperaturePlaceholder);
}

inline const char *clock_weather_humidity_placeholder()
{
    return ui_text(UiTextId::ClockWeatherHumidityPlaceholder);
}
