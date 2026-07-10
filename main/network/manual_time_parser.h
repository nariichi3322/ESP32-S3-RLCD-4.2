// 声明配网页手动离线时间的纯解析与校验接口。
#pragma once

#include <time.h>

inline constexpr int kManualTimeTmYearOffset = 1900;
inline constexpr int kManualTimeTmMonthOffset = 1;

bool parse_manual_datetime_text(const char *text, struct tm *out);
