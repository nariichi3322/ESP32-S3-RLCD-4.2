// 验证 OTA manifest JSON 字段、可选值和错误状态解析规则。
#include "ota_manifest_parser.h"

#include <assert.h>
#include <string.h>

namespace {
constexpr const char *kValidSha =
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";

void expect_missing_field(const char *json,
                          bool have_version,
                          bool have_url,
                          bool have_sha256)
{
    OtaManifest manifest;
    OtaManifestParseResult result = ota_parse_manifest_json(json, &manifest);
    assert(result.status == kOtaManifestParseMissingRequiredFields);
    assert(result.have_version == have_version);
    assert(result.have_url == have_url);
    assert(result.have_sha256 == have_sha256);
}
} // namespace

int main()
{
    OtaManifest manifest;
    OtaManifestParseResult result = ota_parse_manifest_json(
        "{\"version\":\"v1.5.7\","
        "\"url\":\"https://example.invalid/weather_clock.bin\","
        "\"sha256\":\"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\","
        "\"size\":123456,\"notes\":\"稳定性更新\"}",
        &manifest);
    assert(result.status == kOtaManifestParseOk);
    assert(result.have_version && result.have_url && result.have_sha256);
    assert(result.sha256_length == 64);
    assert(strcmp(manifest.version, "v1.5.7") == 0);
    assert(strcmp(manifest.url, "https://example.invalid/weather_clock.bin") == 0);
    assert(strcmp(manifest.sha256, kValidSha) == 0);
    assert(strcmp(manifest.notes, "稳定性更新") == 0);
    assert(manifest.size == 123456);

    OtaManifest optional_fields;
    optional_fields.size = 321;
    strcpy(optional_fields.notes, "保留");
    result = ota_parse_manifest_json(
        "{\"version\":\"v1.5.7\","
        "\"url\":\"https://example.invalid/weather_clock.bin\","
        "\"sha256\":\"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\"}",
        &optional_fields);
    assert(result.status == kOtaManifestParseOk);
    assert(optional_fields.size == 321);
    assert(strcmp(optional_fields.notes, "保留") == 0);

    assert(ota_parse_manifest_json(nullptr, &manifest).status ==
           kOtaManifestParseInvalidArgument);
    assert(ota_parse_manifest_json("{}", nullptr).status ==
           kOtaManifestParseInvalidArgument);
    assert(ota_parse_manifest_json("{", &manifest).status ==
           kOtaManifestParseInvalidJson);

    expect_missing_field(
        "{\"url\":\"https://example.invalid/a.bin\","
        "\"sha256\":\"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\"}",
        false,
        true,
        true);
    expect_missing_field(
        "{\"version\":\"v1.5.7\","
        "\"sha256\":\"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\"}",
        true,
        false,
        true);
    expect_missing_field(
        "{\"version\":\"v1.5.7\",\"url\":\"https://example.invalid/a.bin\"}",
        true,
        true,
        false);
    expect_missing_field(
        "{\"version\":\"\",\"url\":\"\","
        "\"sha256\":\"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\"}",
        false,
        false,
        true);

    result = ota_parse_manifest_json(
        "{\"version\":\"v1.5.7\",\"url\":\"https://example.invalid/a.bin\","
        "\"sha256\":\"not-a-valid-sha\"}",
        &manifest);
    assert(result.status == kOtaManifestParseInvalidSha256);
    assert(result.sha256_length == strlen("not-a-valid-sha"));
    return 0;
}
