#include "ui_language_internal.h"

#include <atomic>
#include <string.h>

namespace {
#if defined(APP_UI_LOCALE)
static_assert(APP_UI_LOCALE >= 0 && APP_UI_LOCALE <= 3,
              "APP_UI_LOCALE must be one of the four supported locale ids");
std::atomic<uint8_t> s_language{static_cast<uint8_t>(APP_UI_LOCALE)};
std::atomic<uint8_t> s_target_language{static_cast<uint8_t>(APP_UI_LOCALE)};
#else
// Host tests and legacy tools retain the old runtime-switching behaviour.
std::atomic<uint8_t> s_language{static_cast<uint8_t>(kDefaultUiLanguage)};
std::atomic<uint8_t> s_target_language{static_cast<uint8_t>(kDefaultUiLanguage)};
#endif
std::atomic<uint32_t> s_language_revision{1};

// Legacy UI widgets still pass their rendered text through
// ui_language_localize(). Keep that compatibility path backed by the same
// generated four-locale catalog as the stable UiTextId API, so a firmware
// image never falls back to English merely because a caller uses an older
// text-producing path.
#if defined(APP_UI_LOCALE)
struct CatalogLiteral {
    const char *traditional;
    const char *simplified;
    const char *english;
    const char *japanese;
    const char *selected;
};
#if APP_UI_LOCALE == 1
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese, simplified }
#elif APP_UI_LOCALE == 2
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese, english }
#elif APP_UI_LOCALE == 3
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese, japanese }
#else
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese, traditional }
#endif
#else
struct CatalogLiteral {
    const char *traditional;
    const char *simplified;
    const char *english;
    const char *japanese;
};
#define UI_CATALOG(traditional, simplified, english, japanese) \
    { traditional, simplified, english, japanese }
#endif

constexpr CatalogLiteral kCatalogLiterals[] = {
#include "generated/ui_catalog_entries.inc"
};

#undef UI_CATALOG

}

UiLanguage normalize_ui_language(uint8_t stored)
{
    switch (stored) {
    case static_cast<uint8_t>(UiLanguage::Simplified):
        return UiLanguage::Simplified;
    case static_cast<uint8_t>(UiLanguage::English):
        return UiLanguage::English;
    case static_cast<uint8_t>(UiLanguage::Japanese):
        return UiLanguage::Japanese;
    default:
        return UiLanguage::Traditional;
    }
}

UiLanguage ui_language_load()
{
    return normalize_ui_language(s_language.load(std::memory_order_acquire));
}

bool ui_language_is_traditional()
{
    return ui_language_load() == UiLanguage::Traditional;
}

bool ui_language_is_simplified()
{
    return ui_language_load() == UiLanguage::Simplified;
}

bool ui_language_is_english()
{
    return ui_language_load() == UiLanguage::English;
}

bool ui_language_is_japanese()
{
    return ui_language_load() == UiLanguage::Japanese;
}

const char *ui_language_text(const char *traditional,
                             const char *simplified,
                             const char *english,
                             const char *japanese)
{
    const UiLanguage language = ui_language_load();
    const char *selected = traditional;
    if (language == UiLanguage::Simplified) {
        selected = simplified;
    } else if (language == UiLanguage::English) {
        selected = english;
    } else if (language == UiLanguage::Japanese) {
        selected = japanese;
    }
    return selected ? selected : "";
}

const char *ui_language_localize(const char *text)
{
    if (!text) return "";
    for (const CatalogLiteral &literal : kCatalogLiterals) {
#if defined(APP_UI_LOCALE)
        if (strcmp(text, literal.traditional) == 0 ||
            strcmp(text, literal.simplified) == 0 ||
            strcmp(text, literal.english) == 0 ||
            strcmp(text, literal.japanese) == 0 ||
            strcmp(text, literal.selected) == 0) {
            return literal.selected;
        }
#else
        if (strcmp(text, literal.traditional) == 0 ||
            strcmp(text, literal.simplified) == 0 ||
            strcmp(text, literal.english) == 0 ||
            strcmp(text, literal.japanese) == 0) {
            return ui_language_text(literal.traditional,
                                    literal.simplified,
                                    literal.english,
                                    literal.japanese);
        }
#endif
    }
    return text;
}

void ui_language_store(UiLanguage language)
{
    const uint8_t normalized = static_cast<uint8_t>(normalize_ui_language(
        static_cast<uint8_t>(language)));
#if defined(APP_UI_LOCALE)
    const uint8_t previous = s_target_language.exchange(normalized,
                                                         std::memory_order_acq_rel);
#else
    const uint8_t previous = s_language.exchange(normalized,
                                                  std::memory_order_acq_rel);
    // Host tests do not reboot into a new compile-time locale, so keep the
    // requested target in step with the runtime language there.
    s_target_language.store(normalized, std::memory_order_release);
#endif
    if (previous != normalized) {
        s_language_revision.fetch_add(1, std::memory_order_acq_rel);
    }
}

UiLanguage ui_language_target()
{
    return normalize_ui_language(s_target_language.load(std::memory_order_acquire));
}

const char *ui_language_locale_tag(UiLanguage language)
{
    switch (normalize_ui_language(static_cast<uint8_t>(language))) {
    case UiLanguage::Simplified: return "zh-CN";
    case UiLanguage::English: return "en";
    case UiLanguage::Japanese: return "ja";
    default: return "zh-TW";
    }
}

const char *ui_language_open_meteo_tag()
{
    const UiLanguage language = ui_language_load();
    return language == UiLanguage::Japanese ? "ja" :
           language == UiLanguage::English ? "en" : "zh";
}

bool ui_language_from_locale_tag(const char *tag, UiLanguage *language)
{
    if (!tag || !language) return false;
    if (strcmp(tag, "zh-TW") == 0) *language = UiLanguage::Traditional;
    else if (strcmp(tag, "zh-CN") == 0) *language = UiLanguage::Simplified;
    else if (strcmp(tag, "en") == 0) *language = UiLanguage::English;
    else if (strcmp(tag, "ja") == 0) *language = UiLanguage::Japanese;
    else return false;
    return true;
}

uint32_t ui_language_revision()
{
    return s_language_revision.load(std::memory_order_acquire);
}
