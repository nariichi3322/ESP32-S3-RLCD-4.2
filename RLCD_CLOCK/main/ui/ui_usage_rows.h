#pragma once

#include "ui_usage_bottom_mode.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

struct CodexUsageStateData;

struct UsageRow {
    lv_obj_t *title = nullptr;
    lv_obj_t *meter = nullptr;
    lv_obj_t *fill = nullptr;
    lv_obj_t *value = nullptr;
    lv_obj_t *sub = nullptr;
    int fill_width = -1;
};

struct UsageBottom {
    lv_obj_t *title = nullptr;
    lv_obj_t *left[2]{};
    lv_obj_t *right[2]{};
    lv_obj_t *message = nullptr;
    lv_obj_t *stats_title[3]{};
    lv_obj_t *stats_value[3]{};
    lv_obj_t *indicator[2]{};
};

lv_obj_t *usage_content_container(lv_obj_t *screen);
UsageRow usage_row_create(lv_obj_t *parent, int y, const char *title,
                          bool meter, int sub_offset = 20);
bool usage_row_update(UsageRow &row, const char *value, const char *sub,
                      int percent, bool show_meter = true);
bool usage_row_quota(UsageRow &row, bool available, uint8_t percent,
                     uint32_t reset_seconds, uint32_t received_tick_ms, uint32_t now);
bool usage_row_window(UsageRow &row, uint32_t minutes, uint32_t expected,
                      const char *default_title);
struct UsageStats {
    char today[32];
    char week[32];
    char running[32];
};
UsageStats usage_format_stats(const CodexUsageStateData &view, bool include_estimate);
void usage_bottom_create(lv_obj_t *parent, UsageBottom &bottom);
bool usage_bottom_update(UsageBottom &bottom, UsageBottomSection section,
                         const char *title, const char *left0, const char *right0,
                         const char *left1, const char *right1,
                         const char *message, bool hide_title,
                         const char *today, const char *week, const char *running);
void usage_format_duration(uint32_t seconds, char *out, size_t size);
void usage_format_date(uint64_t seconds, char *out, size_t size);

// Avoid overflowing when a BLE amount approaches UINT64_MAX.
inline unsigned usage_spent_percent(uint64_t used, uint64_t limit)
{
    if (!limit) return 0;
    const uint64_t whole = used / limit;
    if (whole >= 10) return 999;
    const uint64_t remainder = used % limit;
    uint64_t accumulated = 0;
    unsigned fraction = 0;
    for (unsigned i = 0; i < 100; ++i) {
        if (accumulated >= limit - remainder) {
            accumulated -= limit - remainder;
            ++fraction;
        } else {
            accumulated += remainder;
        }
    }
    return static_cast<unsigned>(whole) * 100U + fraction;
}

inline void usage_format_uint64(uint64_t value, char *out, size_t size)
{
    char reverse[20];
    size_t count = 0;
    do {
        reverse[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value);
    if (!size) return;
    size_t written = 0;
    while (count && written + 1 < size) out[written++] = reverse[--count];
    out[written] = '\0';
}

inline void usage_format_amount(uint64_t minor, uint8_t exponent, char *out, size_t size)
{
    uint64_t scale = 1;
    for (uint8_t i = 0; i < exponent && i < 9; ++i) scale *= 10U;
    uint64_t whole = minor / scale;
    if (whole >= 1000) {
        const uint64_t fraction = minor % scale;
        if (fraction && fraction >= (scale + 1U) / 2U) ++whole;
        usage_format_uint64(whole, out, size);
    } else if (exponent) {
        char whole_text[21];
        usage_format_uint64(whole, whole_text, sizeof(whole_text));
        snprintf(out, size, "%s.%0*lu", whole_text, static_cast<int>(exponent),
                 static_cast<unsigned long>(minor % scale));
    } else {
        usage_format_uint64(whole, out, size);
    }
}
