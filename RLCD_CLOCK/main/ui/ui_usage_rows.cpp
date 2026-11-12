#include "ui_usage_rows.h"

#include "ui_fonts.h"
#include "ui_i18n.h"
#include "ui_widgets.h"
#include "ui_page_state.h"
#include "codex_usage_state.h"

#include <limits>
#include <string.h>
#include <time.h>

namespace {
lv_obj_t *body_label(lv_obj_t *parent, int x, int y, int width, int height,
                     const char *text)
{
    lv_obj_t *label = make_label_with_font(parent, x, y, width, height, text,
                                           ui_font(UiFontRole::Body16));
    if (label) {
        lv_obj_set_style_text_font(label, ui_font(UiFontRole::Body16), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(label, width, height);
    }
    return label;
}

bool update_value(lv_obj_t *label, const char *text)
{
    if (!label) return false;
    const lv_font_t *font = ui_font_for_text(text, &lv_font_montserrat_24);
    if (lv_txt_get_width(text, strlen(text), font, 0, LV_TEXT_FLAG_NONE) > 82) {
        font = ui_font_for_text(text, &lv_font_montserrat_16);
    }
    bool changed = lv_obj_get_style_text_font(label, LV_PART_MAIN) != font;
    changed |= set_label_text_if_changed(label, text);
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    }
    return changed;
}
}

lv_obj_t *usage_content_container(lv_obj_t *screen)
{
    lv_obj_t *content = lv_obj_create(screen);
    if (!content) return nullptr;
    lv_obj_set_pos(content, 0, 67);
    lv_obj_set_size(content, 400, 233);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    return content;
}

UsageRow usage_row_create(lv_obj_t *parent, int y, const char *title,
                          bool meter, int sub_offset)
{
    UsageRow row{};
    row.title = body_label(parent, 18, y, 78, 18, title);
    if (meter) {
        row.meter = lv_obj_create(parent);
        if (row.meter) {
            lv_obj_set_pos(row.meter, 96, y + 3);
            lv_obj_set_size(row.meter, 200, 14);
            lv_obj_set_style_radius(row.meter, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row.meter, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_color(row.meter, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_border_width(row.meter, 1, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row.meter, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row.meter, LV_OBJ_FLAG_SCROLLABLE);
            // The inner fill coordinates are relative to the content area inside the border.
            row.fill = make_black_bar(row.meter, 1, 1, 1, 10);
            if (row.fill) lv_obj_set_style_radius(row.fill, 0, LV_PART_MAIN);
        }
    }
    row.value = make_label_with_font(parent, 300, y - 4, 82, 29,
                                     ui_text(UiTextId::UiPlaceholder),
                                     &lv_font_montserrat_24);
    if (row.value) {
        lv_label_set_long_mode(row.value, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.value, 82, 29);
        lv_obj_set_style_text_align(row.value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    row.sub = body_label(parent, 96, y + sub_offset, meter ? 286 : 200, 19, "");
    return row;
}

bool usage_row_update(UsageRow &row, const char *value, const char *sub,
                      int percent, bool show_meter)
{
    bool changed = update_value(row.value, value);
    changed |= set_label_text_if_changed(row.sub, sub);
    if (row.meter) changed |= set_obj_visible(row.meter, show_meter);
    if (row.fill) {
        if (percent > 100) percent = 100;
        const int width = percent > 0 ? 196 * percent / 100 : 0;
        changed |= set_obj_visible(row.fill, width > 0);
        if (width > 0 && row.fill_width != width) {
            lv_obj_set_width(row.fill, width);
            row.fill_width = width;
            changed = true;
        }
    }
    return changed;
}

bool usage_row_quota(UsageRow &row, bool available, uint8_t percent,
                     uint32_t reset_seconds, uint32_t received_tick_ms, uint32_t now)
{
    if (!available) {
        return usage_row_update(row, ui_text(UiTextId::UiPlaceholder),
                                ui_text(UiTextId::CodexResetPlaceholder), 0);
    }
    char value[16];
    char duration[48];
    char reset[80];
    snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(percent));
    usage_format_duration(codex_usage_countdown_seconds(reset_seconds, received_tick_ms, now),
                          duration, sizeof(duration));
    snprintf(reset, sizeof(reset), ui_format(UiTextId::CodexResetFormat), duration);
    return usage_row_update(row, value, reset, percent);
}

bool usage_row_window(UsageRow &row, uint32_t minutes, uint32_t expected,
                      const char *default_title)
{
    if (!minutes || minutes == expected) return set_label_text_if_changed(row.title, default_title);
    char window[16];
    codex_usage_format_window(minutes, window, sizeof(window));
    return set_label_text_if_changed(row.title, window);
}

UsageStats usage_format_stats(const CodexUsageStateData &view, bool include_estimate)
{
    UsageStats stats{};
    if (!view.snapshot_valid || !view.data_valid) {
        snprintf(stats.today, sizeof(stats.today), "%s", ui_text(UiTextId::UiPlaceholder));
        snprintf(stats.week, sizeof(stats.week), "%s", ui_text(UiTextId::UiPlaceholder));
        snprintf(stats.running, sizeof(stats.running), "%s", ui_text(UiTextId::UiPlaceholder));
        return stats;
    }
    char token[24];
    codex_usage_format_tokens(view.snapshot.tokens_today, token, sizeof(token));
    snprintf(stats.today, sizeof(stats.today), "%s%s",
             include_estimate && view.snapshot.tokens_today_estimated ? "~" : "", token);
    codex_usage_format_tokens(view.snapshot.tokens_7d, stats.week, sizeof(stats.week));
    snprintf(stats.running, sizeof(stats.running), "%u", view.snapshot.active_threads);
    return stats;
}

void usage_bottom_create(lv_obj_t *parent, UsageBottom &bottom)
{
    make_black_bar(parent, 18, 176, 364, 1);
    bottom.title = body_label(parent, 18, 183, 78, 19, "");
    for (int i = 0; i < 2; ++i) {
        bottom.left[i] = body_label(parent, 96, 183 + i * 22, 155, 19, "");
        bottom.right[i] = body_label(parent, 253, 183 + i * 22, 129, 19, "");
        if (bottom.right[i]) {
            lv_obj_set_style_text_align(bottom.right[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        }
    }
    bottom.message = body_label(parent, 96, 183, 286, 19, "");
    const UiTextId titles[] = {UiTextId::CodexToday, UiTextId::CodexSevenDays,
                               UiTextId::UsageRunning};
    for (int i = 0; i < 3; ++i) {
        const int x = 96 + i * 96;
        const int width = i == 2 ? 94 : 90;
        bottom.stats_title[i] = body_label(parent, x, 183, width, 19, ui_text(titles[i]));
        bottom.stats_value[i] = body_label(parent, x, 205, width, 19, "");
    }
    for (int i = 0; i < 2; ++i) {
        bottom.indicator[i] = lv_obj_create(parent);
        if (!bottom.indicator[i]) continue;
        lv_obj_set_pos(bottom.indicator[i], 18 + i * 12, 212);
        lv_obj_set_size(bottom.indicator[i], 8, 8);
        lv_obj_set_style_radius(bottom.indicator[i], 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(bottom.indicator[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bottom.indicator[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bottom.indicator[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(bottom.indicator[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

bool usage_bottom_update(UsageBottom &bottom, UsageBottomSection section,
                         const char *title, const char *left0, const char *right0,
                         const char *left1, const char *right1,
                         const char *message, bool hide_title,
                         const char *today, const char *week, const char *running)
{
    const bool stats = section == UsageBottomSection::Stats;
    bool changed = set_label_text_if_changed(bottom.title, title);
    changed |= set_obj_visible(bottom.title, stats || !hide_title);
    const char *left[] = {left0, left1};
    const char *right[] = {right0, right1};
    const char *values[] = {today, week, running};
    for (int i = 0; i < 2; ++i) {
        changed |= set_obj_visible(bottom.left[i], !stats && !message);
        changed |= set_obj_visible(bottom.right[i], !stats && !message);
        changed |= set_label_text_if_changed(bottom.left[i], left[i]);
        changed |= set_label_text_if_changed(bottom.right[i], right[i]);
        if (bottom.indicator[i]) {
            const lv_color_t color = (i == (stats ? 1 : 0)) ? lv_color_black() : lv_color_white();
            if (lv_color_to32(lv_obj_get_style_bg_color(bottom.indicator[i], LV_PART_MAIN)) !=
                lv_color_to32(color)) {
                lv_obj_set_style_bg_color(bottom.indicator[i], color, LV_PART_MAIN);
                changed = true;
            }
        }
    }
    changed |= set_obj_visible(bottom.message, !stats && message);
    changed |= set_label_text_if_changed(bottom.message, message ? message : "");
    for (int i = 0; i < 3; ++i) {
        changed |= set_obj_visible(bottom.stats_title[i], stats);
        changed |= set_obj_visible(bottom.stats_value[i], stats);
        changed |= set_label_text_if_changed(bottom.stats_value[i], values[i]);
    }
    return changed;
}

void usage_format_duration(uint32_t seconds, char *out, size_t size)
{
    const uint32_t minutes = seconds / 60U;
    const uint32_t hours = minutes / 60U;
    const uint32_t days = hours / 24U;
    if (days) {
        snprintf(out, size, ui_format(UiTextId::CodexDurationDaysHours),
                 static_cast<unsigned long>(days), static_cast<unsigned long>(hours % 24U));
    } else if (hours) {
        snprintf(out, size, ui_format(UiTextId::CodexDurationHoursMinutes),
                 static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes % 60U));
    } else {
        snprintf(out, size, ui_format(UiTextId::CodexDurationMinutes),
                 static_cast<unsigned long>(minutes));
    }
}

void usage_format_date(uint64_t seconds, char *out, size_t size)
{
    if (seconds && seconds <= static_cast<uint64_t>(std::numeric_limits<time_t>::max())) {
        const time_t value = static_cast<time_t>(seconds);
        struct tm local{};
        if (localtime_r(&value, &local)) {
            snprintf(out, size, "%d/%d", local.tm_mon + 1, local.tm_mday);
            return;
        }
    }
    snprintf(out, size, "%s", ui_text(UiTextId::UiPlaceholder));
}
