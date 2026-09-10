// 声明 OTA manifest 数据结构、纯 JSON 解析结果和解析入口。
#pragma once

#include "ota_manifest_limits.h"
#include "ui_language.h"

#include <stddef.h>

struct OtaManifestImage {
    char url[kOtaUrlLen] = {};
    char sha256[kOtaSha256Len] = {};
    int size = 0;
    char locale[kOtaLocaleLen] = {};
};

inline constexpr int kOtaManifestImageCount = 4;

struct OtaManifest {
    char version[kOtaVersionLen] = {};
    char url[kOtaUrlLen] = {};
    char sha256[kOtaSha256Len] = {};
    int size = 0;
    char locale[kOtaLocaleLen] = {};
    OtaManifestImage images[kOtaManifestImageCount] = {};
    uint8_t image_mask = 0;
};

enum OtaManifestParseStatus {
    kOtaManifestParseOk = 0,
    kOtaManifestParseInvalidArgument,
    kOtaManifestParseInvalidJson,
    kOtaManifestParseMissingRequiredFields,
    kOtaManifestParseInvalidSha256,
    kOtaManifestParseInvalidLocale,
    kOtaManifestParseInvalidImage,
};

struct OtaManifestParseResult {
    OtaManifestParseStatus status = kOtaManifestParseInvalidArgument;
    bool have_version = false;
    bool have_url = false;
    bool have_sha256 = false;
    size_t sha256_length = 0;
};

OtaManifestParseResult ota_parse_manifest_json(const char *json, OtaManifest *manifest);

// Selects the requested image while preserving the complete parsed map for
// diagnostics and backup-source validation. Legacy manifests expose only the
// top-level zh-TW image and remain valid.
bool ota_manifest_select_image(OtaManifest *manifest, UiLanguage language);
const OtaManifestImage *ota_manifest_image_for_locale(const OtaManifest &manifest,
                                                      UiLanguage language);
