// 定义每日文字缓存的稳定容量，供抓取、状态快照和可见页面共同使用。
#pragma once

inline constexpr int kDailySayingLen = 160;

static_assert(kDailySayingLen > 1,
              "daily saying cache must fit text and a terminator");
