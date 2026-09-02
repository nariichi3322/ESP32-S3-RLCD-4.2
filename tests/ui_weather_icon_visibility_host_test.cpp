#include "ui_weather_icon_visibility.h"

#include <assert.h>

int main()
{
    assert(!weather_ui_icon_visible(false, false));
    assert(!weather_ui_icon_visible(false, true));
    assert(!weather_ui_icon_visible(true, false));
    assert(weather_ui_icon_visible(true, true));
    return 0;
}
