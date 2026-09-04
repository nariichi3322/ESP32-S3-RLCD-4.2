#pragma once

#include <stdint.h>

enum class UiLanguage : uint8_t {
    Traditional = 0,
    Simplified = 1,
    English = 2,
};

inline constexpr UiLanguage kDefaultUiLanguage = UiLanguage::Traditional;

UiLanguage ui_language_load();
bool ui_language_is_traditional();
bool ui_language_is_simplified();
bool ui_language_is_english();
const char *ui_language_text(const char *traditional,
                             const char *simplified,
                             const char *english = nullptr);
const char *ui_language_localize(const char *text);
UiLanguage normalize_ui_language(uint8_t stored);
uint32_t ui_language_revision();
