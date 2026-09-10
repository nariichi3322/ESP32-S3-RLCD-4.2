// 實作 OTA manifest 的舊欄位與多語言 image map 解析。
#include "ota_manifest_parser.h"

#include "network_json.h"
#include "network_json_root.h"
#include "ota_validation.h"

#include "cJSON.h"
#include <limits.h>
#include <string.h>

namespace {
constexpr const char *kVersionField = "version";
constexpr const char *kUrlField = "url";
constexpr const char *kSha256Field = "sha256";
constexpr const char *kSizeField = "size";
constexpr const char *kLocaleField = "locale";
constexpr const char *kImagesField = "images";

struct LocaleEntry {
    UiLanguage language;
    const char *tag;
};

constexpr LocaleEntry kLocaleEntries[] = {
    {UiLanguage::Traditional, "zh-TW"},
    {UiLanguage::Simplified, "zh-CN"},
    {UiLanguage::English, "en"},
    {UiLanguage::Japanese, "ja"},
};

int locale_index(UiLanguage language)
{
    const UiLanguage normalized = normalize_ui_language(static_cast<uint8_t>(language));
    for (int i = 0; i < kOtaManifestImageCount; ++i) {
        if (kLocaleEntries[i].language == normalized) return i;
    }
    return -1;
}

bool copy_locale(const cJSON *object, char *out, size_t out_len)
{
    return json_copy_string_exact(object, kLocaleField, out, out_len) &&
           out[0] != '\0';
}

bool locale_matches(const char *tag, UiLanguage language)
{
    return tag && strcmp(tag, ui_language_locale_tag(language)) == 0;
}

bool parse_image(const cJSON *object,
                 const char *key,
                 OtaManifestImage *image,
                 UiLanguage language)
{
    if (!cJSON_IsObject(object) || !key || !image) return false;
    *image = OtaManifestImage{};
    const cJSON *url = cJSON_GetObjectItem(object, kUrlField);
    const cJSON *sha = cJSON_GetObjectItem(object, kSha256Field);
    const cJSON *size = cJSON_GetObjectItem(object, kSizeField);
    if (!cJSON_IsString(url) || !url->valuestring ||
        !json_copy_string_exact(object, kUrlField, image->url, sizeof(image->url)) ||
        image->url[0] == '\0' || !cJSON_IsString(sha) || !sha->valuestring ||
        !json_copy_string_exact(object, kSha256Field, image->sha256, sizeof(image->sha256)) ||
        !ota_valid_sha256_string(image->sha256) ||
        !copy_locale(object, image->locale, sizeof(image->locale)) ||
        !locale_matches(image->locale, language)) {
        return false;
    }
    if (cJSON_IsNumber(size)) {
        if (size->valuedouble < 0 || size->valuedouble > INT_MAX) return false;
        image->size = size->valueint;
    }
    return true;
}

bool parse_images(const cJSON *images, OtaManifest *manifest)
{
    if (!images) return true;
    if (!cJSON_IsObject(images)) return false;
    for (int i = 0; i < kOtaManifestImageCount; ++i) {
        const cJSON *object = cJSON_GetObjectItem(images, kLocaleEntries[i].tag);
        if (!object) continue;
        if (!parse_image(object,
                         kLocaleEntries[i].tag,
                         &manifest->images[i],
                         kLocaleEntries[i].language)) {
            return false;
        }
        manifest->image_mask = static_cast<uint8_t>(manifest->image_mask | (1U << i));
    }
    return true;
}
} // namespace

OtaManifestParseResult ota_parse_manifest_json(const char *json, OtaManifest *manifest)
{
    OtaManifestParseResult result = {};
    if (!json || !manifest) return result;
    *manifest = OtaManifest{};

    NetworkJsonRoot root(json);
    if (!root) {
        result.status = kOtaManifestParseInvalidJson;
        return result;
    }

    result.have_version = json_copy_string_exact(root.get(), kVersionField,
                                                 manifest->version,
                                                 sizeof(manifest->version)) &&
                          manifest->version[0] != '\0';
    result.have_url = json_copy_string_exact(root.get(), kUrlField,
                                             manifest->url,
                                             sizeof(manifest->url)) &&
                      manifest->url[0] != '\0';
    result.have_sha256 = json_copy_string_exact(root.get(), kSha256Field,
                                                manifest->sha256,
                                                sizeof(manifest->sha256));
    const cJSON *size = cJSON_GetObjectItem(root.get(), kSizeField);
    if (cJSON_IsNumber(size) && size->valuedouble >= 0 && size->valuedouble <= INT_MAX) {
        manifest->size = size->valueint;
    }
    if (!result.have_version || !result.have_url || !result.have_sha256) {
        result.status = kOtaManifestParseMissingRequiredFields;
        return result;
    }
    result.sha256_length = strlen(manifest->sha256);
    if (!ota_valid_sha256_string(manifest->sha256)) {
        result.status = kOtaManifestParseInvalidSha256;
        return result;
    }

    UiLanguage top_language = UiLanguage::Traditional;
    const cJSON *locale = cJSON_GetObjectItem(root.get(), kLocaleField);
    if (!locale) {
        strlcpy(manifest->locale, "zh-TW", sizeof(manifest->locale));
    } else if (!cJSON_IsString(locale) || !locale->valuestring ||
               strlen(locale->valuestring) >= sizeof(manifest->locale) ||
               !ui_language_from_locale_tag(locale->valuestring, &top_language)) {
        result.status = kOtaManifestParseInvalidLocale;
        return result;
    } else {
        strlcpy(manifest->locale, locale->valuestring, sizeof(manifest->locale));
    }

    // The top-level legacy fields are always the zh-TW image. This makes an
    // old manifest usable by the new parser and gives the map a stable entry.
    const int top_index = locale_index(top_language);
    if (top_index >= 0) {
        manifest->images[top_index].size = manifest->size;
        strlcpy(manifest->images[top_index].url,
                manifest->url,
                sizeof(manifest->images[top_index].url));
        strlcpy(manifest->images[top_index].sha256,
                manifest->sha256,
                sizeof(manifest->images[top_index].sha256));
        strlcpy(manifest->images[top_index].locale,
                manifest->locale,
                sizeof(manifest->images[top_index].locale));
        manifest->image_mask = static_cast<uint8_t>(1U << top_index);
    }

    if (!parse_images(cJSON_GetObjectItem(root.get(), kImagesField), manifest)) {
        result.status = kOtaManifestParseInvalidImage;
        return result;
    }
    result.status = kOtaManifestParseOk;
    return result;
}

const OtaManifestImage *ota_manifest_image_for_locale(const OtaManifest &manifest,
                                                      UiLanguage language)
{
    const int index = locale_index(language);
    if (index < 0 || (manifest.image_mask & (1U << index)) == 0) return nullptr;
    return &manifest.images[index];
}

bool ota_manifest_select_image(OtaManifest *manifest, UiLanguage language)
{
    if (!manifest) return false;
    const OtaManifestImage *image = ota_manifest_image_for_locale(*manifest, language);
    if (!image) return false;
    strlcpy(manifest->url, image->url, sizeof(manifest->url));
    strlcpy(manifest->sha256, image->sha256, sizeof(manifest->sha256));
    strlcpy(manifest->locale, image->locale, sizeof(manifest->locale));
    manifest->size = image->size;
    return true;
}
