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
        return {"WAITING", false, 0, "--", "--", "--", 0, 0, "--"};
    if (sdl_preview_mode::is(mode, "codex_stale"))
        return {"STALE", true, 42, "1h 18m", "~84.2K", "531K", 2, 3, "2d 4h"};
    if (sdl_preview_mode::is(mode, "codex_estimated"))
        return {"LINKED", true, 73, "3h 44m", "~21.7K", "302K", 1, 9, "4m"};
    if (sdl_preview_mode::is(mode, "codex_large_tokens"))
        return {"LINKED", true, 99, "6d 23h", "~98.7M", "1.24B", 128, 999, "29d 23h"};
    return {"LINKED", true, 68, "2h 31m", "35.4K", "241K", 3, 7, "1d 6h"};
}

void build_codex_preview_body(lv_obj_t *screen, const char *mode)
{
    const CodexPreviewFixture fixture = codex_preview_fixture(mode);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 76, 170, 20, "CODEX LEFT", &lv_font_montserrat_14);
    char text[24];
    if (fixture.data_valid) {
        snprintf(text, sizeof(text), "%u%%",
                 static_cast<unsigned>(fixture.remaining_percent));
    } else {
        snprintf(text, sizeof(text), "--");
    }
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 96, 170, 62, text, &lv_font_montserrat_48);
    char reset[32];
    snprintf(reset, sizeof(reset), "RESET %s", fixture.reset);
    sdl_preview_widgets::make_label_with_font(
        screen, 18, 160, 170, 25, reset, &lv_font_montserrat_16);
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
        screen, 302, 207, 80, 26, fixture.state, &lv_font_montserrat_14);
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
