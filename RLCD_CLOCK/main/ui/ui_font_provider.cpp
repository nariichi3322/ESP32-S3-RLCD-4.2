#include "ui_fonts.h"

#include "ui_language.h"

#include <stdint.h>

namespace {
#if !defined(APP_UI_LOCALE) || APP_UI_LOCALE == 3
const lv_font_t *japanese_font()
{
#if defined(APP_UI_LOCALE) && APP_UI_LOCALE == 3
    return &lv_font_ja_extra;
#else
    return &lv_font_simsun_16_cjk;
#endif
}
#endif

bool text_contains_cjk(const char *text)
{
    if (!text) return false;
    const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
    while (*cursor) {
        uint32_t codepoint = *cursor++;
        if (codepoint < 0x80U) continue;
        if ((codepoint & 0xe0U) == 0xc0U && cursor[0]) {
            codepoint = ((codepoint & 0x1fU) << 6) | (cursor[0] & 0x3fU);
            ++cursor;
        } else if ((codepoint & 0xf0U) == 0xe0U && cursor[0] && cursor[1]) {
            codepoint = ((codepoint & 0x0fU) << 12) |
                        ((cursor[0] & 0x3fU) << 6) | (cursor[1] & 0x3fU);
            cursor += 2;
        } else if ((codepoint & 0xf8U) == 0xf0U && cursor[0] && cursor[1] && cursor[2]) {
            codepoint = ((codepoint & 0x07U) << 18) |
                        ((cursor[0] & 0x3fU) << 12) |
                        ((cursor[1] & 0x3fU) << 6) | (cursor[2] & 0x3fU);
            cursor += 3;
        } else {
            continue;
        }
        if ((codepoint >= 0x3000U && codepoint <= 0x30ffU) ||
            (codepoint >= 0x3400U && codepoint <= 0x4dbfU) ||
            (codepoint >= 0x4e00U && codepoint <= 0x9fffU) ||
            (codepoint >= 0xff00U && codepoint <= 0xffefU)) {
            return true;
        }
    }
    return false;
}
} // namespace

const lv_font_t *ui_font(UiFontRole role)
{
#if defined(APP_UI_LOCALE) && APP_UI_LOCALE == 3
    if (role == UiFontRole::Metric16) return &lv_font_montserrat_16;
    return japanese_font();
#elif defined(APP_UI_LOCALE) && APP_UI_LOCALE == 2
    switch (role) {
    case UiFontRole::Metric16: return &lv_font_montserrat_16;
    case UiFontRole::Calendar22: return &lv_font_montserrat_16;
    case UiFontRole::Pomodoro24: return &lv_font_montserrat_24;
    default: return &lv_font_montserrat_16;
    }
#else
    switch (role) {
    case UiFontRole::Metric16: return &lv_font_montserrat_16;
    case UiFontRole::Calendar22: return &zh_flip_lunar_22;
    case UiFontRole::Pomodoro24: return &zh_pomodoro_title_24;
    default: return &zh_font_16;
    }
#endif
}

const lv_font_t *ui_font_for_text(const char *text,
                                  const lv_font_t *preferred)
{
    if (!preferred) return ui_font(UiFontRole::Body16);
    if (text_contains_cjk(text)) {
#if defined(APP_UI_LOCALE) && APP_UI_LOCALE == 3
        return japanese_font();
#elif defined(APP_UI_LOCALE) && (APP_UI_LOCALE == 0 || APP_UI_LOCALE == 1)
        return ui_font(UiFontRole::Body16);
#elif defined(APP_UI_LOCALE) && APP_UI_LOCALE == 2
        return &zh_font_16;
#elif !defined(APP_UI_LOCALE)
        if (ui_language_is_japanese()) return japanese_font();
        return ui_font(UiFontRole::Body16);
#endif
    }
    return preferred;
}
