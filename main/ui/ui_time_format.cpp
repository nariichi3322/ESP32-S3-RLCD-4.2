// 将有效系统时间格式化为完整日期时间，无效值统一输出占位符。
#include "ui_time_format.h"

#include "app_time_constants.h"
#include "ui_i18n.h"
#include "ui_text_format.h"

namespace {
constexpr size_t kDateTimeTextSize = 32;
void copy_invalid_time_text(char *out, size_t out_len)
{
    ui_text_format::copy(out, out_len, ui_text(UiTextId::UiPlaceholder));
}

bool time_year_valid(int year)
{
    return year >= kMinValidYear && year <= kMaxValidYear;
}

void format_full_datetime_text(char *out, size_t out_len, const struct tm &local, int year)
{
    char formatted[kDateTimeTextSize] = {};
    int written = snprintf(formatted, sizeof(formatted), ui_format(UiTextId::FullDateTimeFormat),
                           year,
                           local.tm_mon + kTmMonthOffset,
                           local.tm_mday,
                           local.tm_hour,
                           local.tm_min,
                           local.tm_sec);
    if (ui_text_format::format_failed(written, sizeof(formatted))) {
        copy_invalid_time_text(out, out_len);
        return;
    }
    ui_text_format::copy(out, out_len, formatted);
}
}

void format_time_or_dash(time_t value, char *out, size_t out_len)
{
    if (!ui_text_format::output_buffer_available(out, out_len)) {
        return;
    }
    if (value <= 0) {
        copy_invalid_time_text(out, out_len);
        return;
    }
    struct tm local = {};
    localtime_r(&value, &local);
    const int year = local.tm_year + kTmYearOffset;
    if (!time_year_valid(year)) {
        copy_invalid_time_text(out, out_len);
        return;
    }
    format_full_datetime_text(out, out_len, local, year);
}
