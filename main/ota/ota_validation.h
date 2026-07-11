// 声明 OTA 版本比较、SHA256 文本校验和十六进制转换接口。
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline constexpr size_t kOtaSha256ByteCount = 32;
inline constexpr size_t kOtaSha256HexLen = kOtaSha256ByteCount * 2;

namespace ota_validation_detail {
inline constexpr int kSemverComponentCount = 3;
inline constexpr char kHexDigits[] = "0123456789abcdef";

inline int parse_semver_component(const char **cursor)
{
    if (!cursor || !*cursor) {
        return 0;
    }
    int value = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10 + (**cursor - '0');
        ++(*cursor);
    }
    if (**cursor == '.') {
        ++(*cursor);
    }
    return value;
}

static_assert(kSemverComponentCount == 3,
              "OTA semantic version comparison expects three components");
static_assert(kOtaSha256ByteCount == 32, "OTA SHA256 byte count must remain 32");
static_assert(kOtaSha256HexLen == kOtaSha256ByteCount * 2,
              "OTA SHA256 hex text must use two characters per byte");
static_assert(sizeof(kHexDigits) == 17, "OTA lowercase hex table must contain 16 digits and NUL");
} // namespace ota_validation_detail

inline int ota_compare_versions(const char *remote, const char *current)
{
    if (!remote || !current) {
        return 0;
    }
    if (*remote == 'v' || *remote == 'V') {
        ++remote;
    }
    if (*current == 'v' || *current == 'V') {
        ++current;
    }
    for (int i = 0; i < ota_validation_detail::kSemverComponentCount; ++i) {
        int remote_component = ota_validation_detail::parse_semver_component(&remote);
        int current_component = ota_validation_detail::parse_semver_component(&current);
        if (remote_component != current_component) {
            return remote_component > current_component ? 1 : -1;
        }
    }
    return strcmp(remote, current);
}

inline bool ota_valid_sha256_string(const char *text)
{
    if (!text || strlen(text) != kOtaSha256HexLen) {
        return false;
    }
    for (const char *cursor = text; *cursor; ++cursor) {
        if (!((*cursor >= '0' && *cursor <= '9') ||
              (*cursor >= 'a' && *cursor <= 'f') ||
              (*cursor >= 'A' && *cursor <= 'F'))) {
            return false;
        }
    }
    return true;
}

inline void ota_sha256_to_hex(const uint8_t *hash, char *out, size_t out_len)
{
    if (!out) {
        return;
    }
    if (out_len <= kOtaSha256HexLen) {
        if (out_len > 0) {
            out[0] = '\0';
        }
        return;
    }
    if (!hash) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < kOtaSha256ByteCount; ++i) {
        out[i * 2] = ota_validation_detail::kHexDigits[hash[i] >> 4];
        out[i * 2 + 1] = ota_validation_detail::kHexDigits[hash[i] & 0x0F];
    }
    out[kOtaSha256HexLen] = '\0';
}
