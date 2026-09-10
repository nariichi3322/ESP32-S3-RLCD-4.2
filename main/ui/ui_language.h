#pragma once

#include <stdint.h>

enum class UiLanguage : uint8_t {
    Traditional = 0,
    Simplified = 1,
    English = 2,
    Japanese = 3,
};

inline constexpr UiLanguage kDefaultUiLanguage = UiLanguage::Traditional;

UiLanguage ui_language_load();
bool ui_language_is_traditional();
bool ui_language_is_simplified();
bool ui_language_is_english();
bool ui_language_is_japanese();
const char *ui_language_text(const char *traditional,
                             const char *simplified,
                             const char *english,
                             const char *japanese);
const char *ui_language_localize(const char *text);
UiLanguage normalize_ui_language(uint8_t stored);
uint32_t ui_language_revision();

// The active locale is compiled into the App image. The target locale is the
// user's requested image and remains independent until that image reboots.
UiLanguage ui_language_target();
const char *ui_language_locale_tag(UiLanguage language);
const char *ui_language_open_meteo_tag();
bool ui_language_from_locale_tag(const char *tag, UiLanguage *language);
