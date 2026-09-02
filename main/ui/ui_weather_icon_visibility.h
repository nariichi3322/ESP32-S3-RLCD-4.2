#pragma once

constexpr bool weather_ui_icon_visible(bool weather_ready, bool item_ready)
{
    return weather_ready && item_ready;
}
