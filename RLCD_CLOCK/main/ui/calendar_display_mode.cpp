// Stores the active calendar display preference independently from UI locale.
#include "calendar_display_mode.h"

#include <atomic>

namespace {
#if defined(APP_UI_LOCALE)
static_assert(APP_UI_LOCALE >= 0 && APP_UI_LOCALE <= 3,
              "APP_UI_LOCALE must be one of the four supported locale ids");
#if APP_UI_LOCALE == 2
constexpr uint8_t kDefaultCalendarDisplayMode =
    static_cast<uint8_t>(CalendarDisplayMode::Off);
#elif APP_UI_LOCALE == 3
constexpr uint8_t kDefaultCalendarDisplayMode =
    static_cast<uint8_t>(CalendarDisplayMode::JapaneseKyureki);
#else
constexpr uint8_t kDefaultCalendarDisplayMode =
    static_cast<uint8_t>(CalendarDisplayMode::ChineseLunar);
#endif
#else
// Host tests and tools default to the Traditional Chinese calendar behavior.
constexpr uint8_t kDefaultCalendarDisplayMode =
    static_cast<uint8_t>(CalendarDisplayMode::ChineseLunar);
#endif

std::atomic<uint8_t> s_calendar_display_mode{kDefaultCalendarDisplayMode};
} // namespace

CalendarDisplayMode normalize_calendar_display_mode(uint8_t stored)
{
    switch (stored) {
    case static_cast<uint8_t>(CalendarDisplayMode::ChineseLunar):
        return CalendarDisplayMode::ChineseLunar;
    case static_cast<uint8_t>(CalendarDisplayMode::JapaneseKyureki):
        return CalendarDisplayMode::JapaneseKyureki;
    case static_cast<uint8_t>(CalendarDisplayMode::Off):
        return CalendarDisplayMode::Off;
    default:
        return default_calendar_display_mode();
    }
}

CalendarDisplayMode default_calendar_display_mode()
{
    return static_cast<CalendarDisplayMode>(kDefaultCalendarDisplayMode);
}

CalendarDisplayMode calendar_display_mode_load()
{
    return normalize_calendar_display_mode(
        s_calendar_display_mode.load(std::memory_order_acquire));
}

void calendar_display_mode_store(CalendarDisplayMode mode)
{
    s_calendar_display_mode.store(
        static_cast<uint8_t>(normalize_calendar_display_mode(
            static_cast<uint8_t>(mode))),
        std::memory_order_release);
}
