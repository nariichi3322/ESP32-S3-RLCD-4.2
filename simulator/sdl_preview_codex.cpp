#include "sdl_preview_codex.h"

#include "sdl_preview_mode.h"
#include "sdl_preview_widgets.h"

#include <stdio.h>

namespace {
lv_obj_t *metric(lv_obj_t *parent, int x, int y, const char *title, const char *value)
{
    sdl_preview_widgets::make_label_with_font(
        parent, x, y, 86, 18, title, &lv_font_montserrat_12);
    return sdl_preview_widgets::make_label_with_font(
        parent, x, y + 19, 86, 29, value, &lv_font_montserrat_16);
}
}

CodexPreviewFixture codex_preview_fixture(const char *mode)
{
    if (sdl_preview_mode::is(mode, "codex_waiting"))
        return {"WAITING", false, 0, "--", "--", false, 0, "--", "--", "--", "--", 0, 0, "--"};
    if (sdl_preview_mode::is(mode, "codex_disconnect"))
        return {"DISCONNECT", true, 68, "5h", "2h 31m", true, 74, "30d", "18d 2h", "35.4K", "241K", 3, 7, "1d 6h"};
    if (sdl_preview_mode::is(mode, "codex_stale"))
        return {"STALE", true, 42, "5h", "1h 18m", true, 55, "7d", "3d 1h", "~84.2K", "531K", 2, 3, "2d 4h"};
    if (sdl_preview_mode::is(mode, "codex_estimated"))
        return {"LINKED", true, 73, "5h", "3h 44m", false, 0, "--", "--", "~21.7K", "302K", 1, 9, "4m"};
    if (sdl_preview_mode::is(mode, "codex_large_tokens"))
        return {"LINKED", true, 99, "5h", "4h 59m", true, 88, "30d", "29d 23h", "~98.7M", "1.24B", 128, 999, "29d 23h"};
    return {"LINKED", true, 68, "5h", "2h 31m", true, 74, "7d", "5d 6h", "35.4K", "241K", 3, 7, "1d 6h"};
}

void build_codex_preview_body(lv_obj_t *screen, const char *mode)
{
    const CodexPreviewFixture fixture = codex_preview_fixture(mode);
    char text[24];
    snprintf(text, sizeof(text), "CODEX LEFT (%s)", fixture.primary_window);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 73, 170, 18, text, &lv_font_montserrat_12);
    if (fixture.data_valid) {
        snprintf(text, sizeof(text), "%u%%",
                 static_cast<unsigned>(fixture.remaining_percent));
    } else {
        snprintf(text, sizeof(text), "--");
    }
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 91, 170, 32, text, &lv_font_montserrat_24);
    char reset[32];
    snprintf(reset, sizeof(reset), "RESET %s", fixture.reset);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 121, 170, 20, reset, &lv_font_montserrat_12);
    sdl_preview_widgets::make_black_bar(screen, 18, 146, 170, 2);
    snprintf(text, sizeof(text), "CODEX LEFT (%s)", fixture.secondary_window);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 152, 170, 18, text, &lv_font_montserrat_12);
    if (fixture.secondary_available)
        snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(fixture.secondary_remaining_percent));
    else
        snprintf(text, sizeof(text), "--");
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 170, 170, 32, text, &lv_font_montserrat_24);
    snprintf(reset, sizeof(reset), "RESET %s", fixture.secondary_reset);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 200, 170, 20, reset, &lv_font_montserrat_12);
    sdl_preview_widgets::make_black_bar(screen, 198, 76, 2, 166);
    metric(screen, 212, 76, "TODAY", fixture.today);
    metric(screen, 302, 76, "7 DAYS", fixture.week);
    if (fixture.data_valid)
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(fixture.active_threads));
    else
        snprintf(text, sizeof(text), "--");
    metric(screen, 212, 132, "RUN", text);
    if (fixture.data_valid)
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(fixture.reset_credits));
    else
        snprintf(text, sizeof(text), "--");
    metric(screen, 302, 132, "CREDITS", text);
    metric(screen, 212, 188, "EXPIRY", fixture.expiry);
    lv_obj_t *state = sdl_preview_widgets::make_label_with_font(
        screen, 290, 202, 92, 31, fixture.state, &lv_font_montserrat_12);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_border_width(state, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(state, lv_color_black(), LV_PART_MAIN);
    if (sdl_preview_mode::is(mode, "codex_pairing")) {
        lv_obj_t *overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_size(overlay, 400, 300);
        lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 8, LV_PART_MAIN);
        lv_obj_set_style_border_color(overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(overlay, lv_color_white(), LV_PART_MAIN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = sdl_preview_widgets::make_label_with_font(
            overlay, 25, 45, 350, 35, "BLUETOOTH PAIRING", &lv_font_montserrat_24);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *hint = sdl_preview_widgets::make_label_with_font(
            overlay, 25, 91, 350, 25, "Enter this code in Windows", &lv_font_montserrat_14);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *code = sdl_preview_widgets::make_label_with_font(
            overlay, 25, 125, 350, 75, "482731", &lv_font_montserrat_48);
        lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}
