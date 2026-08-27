// 以原子狀態維護天氣時鐘是否顯示秒數。
#include "ui_clock_seconds_state_internal.h"

#include <atomic>

namespace {
std::atomic<bool> s_weather_clock_seconds_visible{
    kDefaultWeatherClockSecondsVisible};
std::atomic<uint32_t> s_weather_clock_seconds_version{0};
} // namespace

bool weather_clock_seconds_visible_load()
{
    return s_weather_clock_seconds_visible.load(std::memory_order_acquire);
}

uint32_t weather_clock_seconds_version_load()
{
    return s_weather_clock_seconds_version.load(std::memory_order_acquire);
}

void weather_clock_seconds_visible_store(bool visible)
{
    const bool previous = s_weather_clock_seconds_visible.exchange(
        visible,
        std::memory_order_acq_rel);
    if (previous != visible) {
        s_weather_clock_seconds_version.fetch_add(1, std::memory_order_release);
    }
}
