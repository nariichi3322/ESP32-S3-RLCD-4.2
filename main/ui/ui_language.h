#pragma once

#include <stdint.h>

enum class UiLanguage : uint8_t {
    Traditional = 0,
    Simplified = 1,
};

inline constexpr UiLanguage kDefaultUiLanguage = UiLanguage::Traditional;

UiLanguage ui_language_load();
bool ui_language_is_traditional();
const char *ui_language_text(const char *traditional, const char *simplified);
const char *ui_language_localize(const char *text);
UiLanguage normalize_ui_language(uint8_t stored);
