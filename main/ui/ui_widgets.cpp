// 创建并更新项目共用的 LVGL 条形块和文本标签。
#include "ui_widgets.h"

#include "app_metadata.h"
#include "ui_fonts.h"
#include "ui_language.h"

#include <esp_log.h>
#include <string.h>

#define UI_BAR_PARENT_UNAVAILABLE_LOG "bar parent unavailable"
#define UI_BAR_INVALID_SIZE_FORMAT "bar invalid size %dx%d"
#define UI_BAR_CREATE_FAILED_LOG "bar create failed"
#define UI_LABEL_PARENT_UNAVAILABLE_LOG "label parent unavailable"
#define UI_LABEL_INVALID_SIZE_FORMAT "label invalid size %dx%d"
#define UI_LABEL_CREATE_FAILED_LOG "label create failed"

namespace {
const char *label_text_or_empty(const char *text)
{
    return text ? text : "";
}

bool text_contains_cjk(const char *text)
{
    if (!text) {
        return false;
    }
    const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
    while (*cursor) {
        uint32_t codepoint = *cursor++;
        if (codepoint < 0x80U) {
            // ASCII is already represented by every UI font in use here.
        } else if ((codepoint & 0xe0U) == 0xc0U && cursor[0]) {
            codepoint = ((codepoint & 0x1fU) << 6) |
                        (static_cast<uint32_t>(cursor[0]) & 0x3fU);
            ++cursor;
        } else if ((codepoint & 0xf0U) == 0xe0U && cursor[0] && cursor[1]) {
            codepoint = ((codepoint & 0x0fU) << 12) |
                        ((static_cast<uint32_t>(cursor[0]) & 0x3fU) << 6) |
                        (static_cast<uint32_t>(cursor[1]) & 0x3fU);
            cursor += 2;
        } else if ((codepoint & 0xf8U) == 0xf0U && cursor[0] && cursor[1] && cursor[2]) {
            codepoint = ((codepoint & 0x07U) << 18) |
                        ((static_cast<uint32_t>(cursor[0]) & 0x3fU) << 12) |
                        ((static_cast<uint32_t>(cursor[1]) & 0x3fU) << 6) |
                        (static_cast<uint32_t>(cursor[2]) & 0x3fU);
            cursor += 3;
        } else {
            // Skip malformed UTF-8 without treating the continuation byte as
            // an independent character.
            continue;
        }
        if ((codepoint >= 0x3000U && codepoint <= 0x303fU) ||
            (codepoint >= 0x3400U && codepoint <= 0x4dbfU) ||
            (codepoint >= 0x4e00U && codepoint <= 0x9fffU) ||
            (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
            (codepoint >= 0xff00U && codepoint <= 0xffefU)) {
            return true;
        }
    }
    return false;
}

const lv_font_t *font_for_label_text(const char *text, const lv_font_t *font)
{
    if (!font) {
        return nullptr;
    }
    const lv_font_t *selected = ui_font_for_text(text, font);
    if (ui_language_is_english() && selected == ui_font(UiFontRole::Body16) &&
        !text_contains_cjk(text)) {
        return &lv_font_montserrat_14;
    }
    return selected;
}

constexpr int kDarkLabelOverdrawPasses = 5;

void warn_if_center_align_failed(lv_obj_t *label, const char *warning)
{
    if (!center_align_label(label)) {
        ESP_LOGW(TAG, "%s", warning && warning[0] ? warning : UI_LABEL_CREATE_FAILED_LOG);
    }
}
}

void set_obj_box(lv_obj_t *obj, int x, int y, int w, int h)
{
    if (!obj) {
        return;
    }
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
}

void set_obj_black(lv_obj_t *obj, bool active)
{
    if (!obj) {
        return;
    }
    lv_obj_set_style_bg_color(obj, active ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 1, LV_PART_MAIN);
}

static void draw_dark_label_bold_pass(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_DRAW_POST) {
        return;
    }

    lv_obj_t *label = lv_event_get_target(event);
    const char *text = label ? lv_label_get_text(label) : nullptr;
    if (!label || !text || text[0] == '\0') {
        return;
    }

    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    if (!draw_ctx) {
        return;
    }

    lv_draw_label_dsc_t draw_dsc;
    lv_draw_label_dsc_init(&draw_dsc);
    lv_obj_init_draw_label_dsc(label, LV_PART_MAIN, &draw_dsc);
    if (draw_dsc.opa <= LV_OPA_MIN || !draw_dsc.font) {
        return;
    }

    // The SimSun CJK font is anti-aliased at 4bpp, but the RLCD eventually
    // stores only black or white. Repeatedly blending at the same position
    // raises a mid-gray glyph pixel above the display threshold; shifting the
    // pass would merely create another mid-gray pixel and would still vanish.
    draw_dsc.ofs_x = 0;
    lv_area_t text_area;
    lv_obj_get_content_coords(label, &text_area);
    for (int pass = 0; pass < kDarkLabelOverdrawPasses; ++pass) {
        lv_draw_label(draw_ctx, &draw_dsc, &text_area, text, nullptr);
    }
}

void style_label_for_dark_background(lv_obj_t *label)
{
    if (!label) {
        return;
    }

    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);

    // Strengthen anti-aliased glyphs for locales whose black-background text
    // is rendered on the RLCD's binary black/white output path. Do not add the
    // extra pass to metric labels or clock digits; those use their own font.
    if ((ui_language_is_japanese() || ui_language_is_english()) &&
        lv_obj_get_style_text_font(label, LV_PART_MAIN) ==
            ui_font(UiFontRole::Body16) &&
        !lv_obj_has_flag(label, LV_OBJ_FLAG_USER_1)) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_USER_1);
        lv_obj_add_event_cb(label,
                            draw_dark_label_bold_pass,
                            LV_EVENT_DRAW_POST,
                            nullptr);
    }
}

lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h)
{
    if (!parent) {
        ESP_LOGW(TAG, "%s", UI_BAR_PARENT_UNAVAILABLE_LOG);
        return nullptr;
    }
    if (w <= 0 || h <= 0) {
        ESP_LOGW(TAG, UI_BAR_INVALID_SIZE_FORMAT, w, h);
        return nullptr;
    }
    lv_obj_t *bar = lv_obj_create(parent);
    if (!bar) {
        ESP_LOGW(TAG, "%s", UI_BAR_CREATE_FAILED_LOG);
        return nullptr;
    }
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_box(bar, x, y, w, h);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    set_obj_black(bar, false);
    return bar;
}

lv_obj_t *make_black_bar(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *bar = make_bar(parent, x, y, w, h);
    set_obj_black(bar, true);
    return bar;
}

lv_obj_t *make_label_with_font(lv_obj_t *parent,
                               int x,
                               int y,
                               int w,
                               int h,
                               const char *text,
                               const lv_font_t *font)
{
    if (!parent) {
        ESP_LOGW(TAG, "%s", UI_LABEL_PARENT_UNAVAILABLE_LOG);
        return nullptr;
    }
    if (w <= 0 || h <= 0) {
        ESP_LOGW(TAG, UI_LABEL_INVALID_SIZE_FORMAT, w, h);
        return nullptr;
    }
    const char *localized_text = ui_language_localize(label_text_or_empty(text));
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        ESP_LOGW(TAG, "%s", UI_LABEL_CREATE_FAILED_LOG);
        return nullptr;
    }
    set_obj_box(label, x, y, w, h);
    lv_label_set_text(label, localized_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    if (font) {
        lv_obj_set_style_text_font(label,
                                   font_for_label_text(localized_text, font),
                                   LV_PART_MAIN);
    }
    lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN);
    return label;
}

lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h, const char *text)
{
    return make_label_with_font(parent, x, y, w, h, text,
                                ui_font(UiFontRole::Body16));
}

bool center_align_label(lv_obj_t *label)
{
    if (!label) {
        return false;
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return true;
}

lv_obj_t *make_centered_label(lv_obj_t *parent,
                              int x,
                              int y,
                              int w,
                              int h,
                              const char *text,
                              const char *warning)
{
    lv_obj_t *label = make_label(parent, x, y, w, h, text);
    warn_if_center_align_failed(label, warning);
    return label;
}

lv_obj_t *make_centered_label_with_font(lv_obj_t *parent,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        const char *text,
                                        const lv_font_t *font,
                                        const char *warning)
{
    lv_obj_t *label = make_label_with_font(parent, x, y, w, h, text, font);
    warn_if_center_align_failed(label, warning);
    return label;
}

bool set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label) {
        return false;
    }
    text = ui_language_localize(label_text_or_empty(text));
    const lv_font_t *current_font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (text_contains_cjk(text) && current_font && !current_font->fallback) {
        lv_obj_set_style_text_font(label,
                                   ui_font_for_text(text, current_font),
                                   LV_PART_MAIN);
    }
    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
        return true;
    }
    return false;
}
