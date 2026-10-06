// Declares the persisted, locale-independent calendar display preference.
#pragma once

#include <stdint.h>

enum class CalendarDisplayMode : uint8_t {
    ChineseLunar = 0,
    JapaneseKyureki = 1,
    Off = 2,
};

CalendarDisplayMode normalize_calendar_display_mode(uint8_t stored);
CalendarDisplayMode default_calendar_display_mode();
CalendarDisplayMode calendar_display_mode_load();
void calendar_display_mode_store(CalendarDisplayMode mode);
