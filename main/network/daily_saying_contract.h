// 定义每日文字缓存容量和联网抓取入口，供抓取、状态与页面共同使用。
#pragma once

inline constexpr int kDailySayingLen = 160;

static_assert(kDailySayingLen > 1,
              "daily saying cache must fit text and a terminator");

bool perform_daily_saying_update();
