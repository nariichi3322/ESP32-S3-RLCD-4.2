// Verifies calendar display mode defaults, normalization, and runtime storage.
#include "calendar_display_mode.h"

#include <assert.h>
#include <stdint.h>

int main()
{
#if defined(APP_UI_LOCALE) && APP_UI_LOCALE == 2
    assert(default_calendar_display_mode() == CalendarDisplayMode::Off);
#elif defined(APP_UI_LOCALE) && APP_UI_LOCALE == 3
    assert(default_calendar_display_mode() == CalendarDisplayMode::JapaneseKyureki);
#else
    assert(default_calendar_display_mode() == CalendarDisplayMode::ChineseLunar);
#endif

    assert(normalize_calendar_display_mode(0) == CalendarDisplayMode::ChineseLunar);
    assert(normalize_calendar_display_mode(1) == CalendarDisplayMode::JapaneseKyureki);
    assert(normalize_calendar_display_mode(2) == CalendarDisplayMode::Off);
    assert(normalize_calendar_display_mode(3) == default_calendar_display_mode());
    assert(normalize_calendar_display_mode(UINT8_MAX) == default_calendar_display_mode());

    calendar_display_mode_store(CalendarDisplayMode::ChineseLunar);
    assert(calendar_display_mode_load() == CalendarDisplayMode::ChineseLunar);
    calendar_display_mode_store(CalendarDisplayMode::JapaneseKyureki);
    assert(calendar_display_mode_load() == CalendarDisplayMode::JapaneseKyureki);
    calendar_display_mode_store(CalendarDisplayMode::Off);
    assert(calendar_display_mode_load() == CalendarDisplayMode::Off);
    calendar_display_mode_store(static_cast<CalendarDisplayMode>(UINT8_MAX));
    assert(calendar_display_mode_load() == default_calendar_display_mode());
    return 0;
}
