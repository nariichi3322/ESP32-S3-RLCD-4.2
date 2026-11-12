#include "sdl_preview_codex.h"

#include "sdl_preview_mode.h"
#include "sdl_preview_widgets.h"
#include "ui_fonts.h"
#include "ui_i18n.h"

#include <stdio.h>
#include <string.h>

namespace {
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, int height,
                const char *text, const lv_font_t *font)
{
    lv_obj_t *obj = sdl_preview_widgets::make_label_with_font(
        parent, x, y, width, height, text, font);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_size(obj, width, height);
    return obj;
}

void row(lv_obj_t *screen, int y, const char *title, const char *value,
         const char *sub, int percent, bool meter)
{
    y += 67;
    label(screen, 18, y, 78, 19, title, ui_font(UiFontRole::Body16));
    if (meter) {
        lv_obj_t *outline = sdl_preview_widgets::make_bar(screen, 96, y + 3, 200, 14);
        lv_obj_set_style_radius(outline, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(outline, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(outline, lv_color_black(), LV_PART_MAIN);
        if (percent > 0) {
            if (percent > 100) percent = 100;
            lv_obj_t *fill = sdl_preview_widgets::make_black_bar(
                screen, 98, y + 5, 196 * percent / 100, 10);
            lv_obj_set_style_radius(fill, 0, LV_PART_MAIN);
        }
    }
    const lv_font_t *font = ui_font_for_text(value, &lv_font_montserrat_24);
    if (lv_txt_get_width(value, strlen(value), font, 0, LV_TEXT_FLAG_NONE) > 82) {
        font = ui_font_for_text(value, &lv_font_montserrat_16);
    }
    lv_obj_t *number = label(screen, 300, y - 4, 82, 29, value, font);
    lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    label(screen, 96, y + (meter ? 20 : 2), meter ? 286 : 200, 19, sub, ui_font(UiFontRole::Body16));
}

void bottom(lv_obj_t *screen, bool stats, const char *title,
            const char *left0, const char *right0, const char *left1,
            const char *right1, const CodexPreviewFixture &fixture)
{
    sdl_preview_widgets::make_black_bar(screen, 18, 67 + 176, 364, 1);
    label(screen, 18, 67 + 183, 78, 19, title, ui_font(UiFontRole::Body16));
    if (stats) {
        const UiTextId titles[] = {UiTextId::CodexToday, UiTextId::CodexSevenDays,
                                   UiTextId::UsageRunning};
        char active[16];
        snprintf(active, sizeof(active), "%u", static_cast<unsigned>(fixture.active_threads));
        const char *values[] = {fixture.today, fixture.week, active};
        for (int i = 0; i < 3; ++i) {
            int x = 96 + i * 96;
            int width = i == 2 ? 94 : 90;
            label(screen, x, 250, width, 19, ui_text(titles[i]), ui_font(UiFontRole::Body16));
            label(screen, x, 272, width, 19, values[i], ui_font(UiFontRole::Body16));
        }
    } else {
        const char *left[] = {left0, left1};
        const char *right[] = {right0, right1};
        for (int i = 0; i < 2; ++i) {
            label(screen, 96, 250 + i * 22, 155, 19, left[i], ui_font(UiFontRole::Body16));
            lv_obj_t *date = label(screen, 253, 250 + i * 22, 129, 19,
                                   right[i], ui_font(UiFontRole::Body16));
            lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        }
    }
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *square = sdl_preview_widgets::make_bar(screen, 18 + i * 12, 279, 8, 8);
        lv_obj_set_style_radius(square, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(square, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(square, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(square, i == (stats ? 1 : 0) ? lv_color_black() : lv_color_white(),
                                  LV_PART_MAIN);
    }
}

void pairing_overlay()
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 400, 300);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(overlay, lv_color_white(), LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(overlay, 25, 45, 350, 35, "BLUETOOTH PAIRING", &lv_font_montserrat_24);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *hint = label(overlay, 25, 91, 350, 25, "Enter this code in Windows", &lv_font_montserrat_14);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *code = label(overlay, 25, 125, 350, 75, "482731", &lv_font_montserrat_48);
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}
}

CodexPreviewFixture codex_preview_fixture(const char *mode)
{
    if (sdl_preview_mode::is(mode, "codex_waiting"))
        return {"WAITING", false, 0, "--", "--", false, 0, "--", "--", "--", "--", 0, "--", 0, "--"};
    if (sdl_preview_mode::is(mode, "codex_disconnect"))
        return {"DISCONNECT", true, 68, "5h", "2h 31m", true, 74, "30d", "18d 2h", "35.4K", "241K", 3, "875", 7, "1d 6h"};
    if (sdl_preview_mode::is(mode, "codex_stale"))
        return {"STALE", true, 42, "5h", "1h 18m", true, 55, "7d", "3d 1h", "~84.2K", "531K", 2, "0", 3, "2d 4h"};
    if (sdl_preview_mode::is(mode, "codex_estimated"))
        return {"LINKED", true, 73, "5h", "3h 44m", false, 0, "--", "--", "~21.7K", "302K", 1, "UNLIM", 9, "4m"};
    if (sdl_preview_mode::is(mode, "codex_large_tokens"))
        return {"LINKED", true, 99, "5h", "4h 59m", true, 88, "30d", "29d 23h", "~98.7M", "1.24B", 128, "1.2M", 255, "29d 23h"};
    return {"LINKED", true, 68, "5h", "2h 31m", true, 74, "7d", "5d 6h", "35.4K", "241K", 3, "875", 7, "1d 6h"};
}

void build_codex_preview_body(lv_obj_t *screen, const char *mode)
{
    if (sdl_preview_mode::is(mode, "codex_offline")) {
        lv_obj_t *title = label(screen, 25, 123, 350, 34, "BLUETOOTH OFF", &lv_font_montserrat_24);
        lv_obj_t *detail = label(screen, 25, 163, 350, 24, "(OFFLINE)", &lv_font_montserrat_16);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }
    const CodexPreviewFixture fixture = codex_preview_fixture(mode);
    const bool claude = sdl_preview_mode::is(mode, "claude") ||
                        sdl_preview_mode::has_prefix(mode, "claude_");
    const bool detail_valid = !sdl_preview_mode::is(mode, "claude_stale") &&
                              !sdl_preview_mode::is(mode, "claude_no_detail");
    const bool stats = sdl_preview_mode::is(mode, "claude_stats") ||
                       sdl_preview_mode::is(mode, "codex_stats");
    char value[32];
    char sub[128];
    const bool primary = fixture.data_valid;
    snprintf(value, sizeof(value), primary ? "%u%%" : "--", static_cast<unsigned>(fixture.remaining_percent));
    snprintf(sub, sizeof(sub), ui_format(UiTextId::CodexResetFormat), fixture.reset);
    row(screen, claude ? 6 : 8, ui_text(UiTextId::UsageFiveHours), value, sub,
        primary ? fixture.remaining_percent : 0, true);
    snprintf(value, sizeof(value), fixture.secondary_available ? "%u%%" : "--",
             static_cast<unsigned>(fixture.secondary_remaining_percent));
    snprintf(sub, sizeof(sub), ui_format(UiTextId::CodexResetFormat), fixture.secondary_reset);
    const char *second_title = strcmp(fixture.secondary_window, "30d") == 0
                                   ? "30d" : ui_text(UiTextId::UsageSevenDays);
    row(screen, claude ? 48 : 56, second_title, value, sub,
        fixture.secondary_available ? fixture.secondary_remaining_percent : 0, true);
    if (claude) {
        snprintf(sub, sizeof(sub), detail_valid ? ui_format(UiTextId::CodexResetFormat) : "%s",
                 detail_valid ? "3d 4h" : "");
        row(screen, 90, ui_text(UiTextId::UsageFable), detail_valid ? "88%" : "--",
            sub, detail_valid ? 88 : 0, true);
        if (detail_valid) snprintf(sub, sizeof(sub), ui_format(UiTextId::UsageSpentBoth), "$7.86", "$40.00");
        else sub[0] = '\0';
        row(screen, 132, ui_text(UiTextId::UsagePaid), detail_valid ? "19%" : "--",
            sub, detail_valid ? 19 : 0, true);
    } else {
        const char *paid = fixture.data_valid
                               ? (strcmp(fixture.paid, "UNLIM") == 0
                                      ? ui_text(UiTextId::CodexUnlimited) : fixture.paid)
                               : ui_text(UiTextId::UiPlaceholder);
        row(screen, 104, ui_text(UiTextId::UsagePaid), paid,
            ui_text(UiTextId::UsageBalance), 0, false);
    }
    char left[128] = "";
    char right[96] = "";
    char left2[128] = "";
    char right2[96] = "";
    char title[64];
    snprintf(title, sizeof(title), "%s", ui_text(stats ? UiTextId::UsageStats : UiTextId::UsageGrants));
    if (claude && detail_valid) {
        snprintf(left, sizeof(left), ui_format(UiTextId::UsageGrantCount), ui_text(UiTextId::UsageGrantFull), "1");
        snprintf(right, sizeof(right), ui_format(UiTextId::UsageExpires), "12/10");
        snprintf(left2, sizeof(left2), ui_format(UiTextId::UsageGrantCount), ui_text(UiTextId::UsageGrantFable), "1");
        snprintf(right2, sizeof(right2), ui_format(UiTextId::UsageExpires), "3/9");
    } else if (claude || !fixture.data_valid) {
        snprintf(left, sizeof(left), "--");
    } else {
        char count[16];
        snprintf(count, sizeof(count), "%u", static_cast<unsigned>(fixture.reset_credits));
        snprintf(left, sizeof(left), ui_format(UiTextId::UsageGrantCount), ui_text(UiTextId::UsageAvailable), count);
        snprintf(right, sizeof(right), ui_format(UiTextId::UsageNearestExpires), "12/10");
    }
    bottom(screen, stats, title, left, right, left2, right2, fixture);
    if (sdl_preview_mode::is(mode, "codex_pairing")) pairing_overlay();
}
