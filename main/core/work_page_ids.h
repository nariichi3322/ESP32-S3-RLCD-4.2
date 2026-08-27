// 统一定义跨 UI、输入、网络和存储使用的稳定工作页编号。
#pragma once

#include <stdint.h>

inline constexpr int kWorkPageWeatherClock = 0;
inline constexpr int kWorkPageGallery = 1;
inline constexpr int kWorkPageWeatherBoard = 2;
inline constexpr int kWorkPageFlipClock = 3;
inline constexpr int kWorkPageCalendar = 4;
inline constexpr int kWorkPageHistory = 5;
inline constexpr int kWorkPageXiaozhiAI = 6;
inline constexpr int kWorkPageCodexUsage = 7;
inline constexpr int kWorkPageCount = 8;

// 显示设置中的页面开关与工作页编号保持一一对应。
inline constexpr int kDisplaySettingsPageItemCount = kWorkPageCount;

constexpr bool is_valid_work_page_id(int page)
{
    return page >= kWorkPageWeatherClock && page < kWorkPageCount;
}

static_assert(kWorkPageWeatherClock == 0,
              "work page ids must start at weather clock zero");
static_assert(kWorkPageCount == kWorkPageCodexUsage + 1,
              "work page count must match the last work page id");
static_assert(kDisplaySettingsPageItemCount == kWorkPageCount,
              "display page setting count must match work page count");
static_assert(is_valid_work_page_id(kWorkPageWeatherClock),
              "first work page id must be valid");
static_assert(is_valid_work_page_id(kWorkPageCodexUsage),
              "last work page id must be valid");
static_assert(kWorkPageCount == static_cast<int>(sizeof(uint8_t) * 8),
              "eight pages exhaust the uint8_t page mask; widen it before adding page nine");
static_assert(!is_valid_work_page_id(-1),
              "negative work page ids must be rejected");
static_assert(!is_valid_work_page_id(kWorkPageCount),
              "work page count is outside the valid id range");
