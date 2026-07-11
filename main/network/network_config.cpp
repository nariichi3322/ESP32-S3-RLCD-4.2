// 负责 Wi-Fi、API Key、页面设置和声音设置的 NVS 配置读写。
#include "network_services.h"
#include "network_form.h"
#include "manual_time_parser.h"
#include "xiaozhi_ai.h"

#include "custom_assets.h"
#include "sensor_services.h"
#include "ui_views.h"

#include <errno.h>

namespace {
constexpr size_t max_size(size_t a, size_t b)
{
    return a > b ? a : b;
}

constexpr const char *kWifiNvsNamespace = "wifi";
constexpr const char *kWifiSsidKey = "ssid";
constexpr const char *kWifiPassKey = "pass";
constexpr const char *kWeatherApiKeyKey = "api_key";
constexpr const char *kManualWeatherCityKey = "weather_city_v1";
constexpr const char *kIgnoredAssetWeatherCityKey = "asset_city_skip";
constexpr const char *kLegacyApiHostKey = "api_host";
constexpr const char *kOfflineModeKey = "offline_v1";
constexpr const char *kHourlyChimeKey = "hourly_chime_v2";
constexpr const char *kHourlyAllDayKey = "hour_all_v1";
constexpr const char *kChimeVolumeKey = "chime_vol_v1";
constexpr const char *kChimeSoundKey = "chime_snd_v1";
constexpr const char *kPageMaskV1Key = "page_mask_v1";
constexpr const char *kPageMaskV2Key = "page_mask_v2";
constexpr const char *kPageMaskV3Key = "page_mask_v3";
constexpr const char *kPageMaskV4Key = "page_mask_v4";
constexpr const char *kPageMaskV5Key = "page_mask_v5";
constexpr const char *kPageOrderV1Key = "page_order_v1";
constexpr const char *kPageOrderV2Key = "page_order_v2";
constexpr const char *kPageOrderV3Key = "page_order_v3";
constexpr const char *kPageOrderV4Key = "page_order_v4";
constexpr const char *kPageOrderV5Key = "page_order_v5";
constexpr size_t kManualTimeFieldSize = 32;
constexpr size_t kSetupSsidFieldSize = 33;
constexpr size_t kSetupPasswordFieldSize = 65;
constexpr size_t kSetupApiKeyFieldSize = 96;
constexpr size_t kSetupWeatherCityFieldSize = kManualWeatherCityLen;
constexpr size_t kMaxSetupFormFieldSize =
    max_size(max_size(max_size(kManualTimeFieldSize, kSetupSsidFieldSize),
                      max_size(kSetupPasswordFieldSize, kSetupApiKeyFieldSize)),
             kSetupWeatherCityFieldSize);
constexpr uint8_t kNvsUnsetU8 = 0xFF;
constexpr uint8_t kDefaultChimeVolumePercent = 80;
constexpr uint8_t kValidChimeVolumePercent[] = {20, 40, 60, 80, 100};
constexpr uint8_t work_page_mask_bit(int page)
{
    return static_cast<uint8_t>(1U << page);
}

constexpr uint8_t all_work_page_mask()
{
    return static_cast<uint8_t>((1U << kWorkPageCount) - 1);
}

constexpr bool work_page_mask_has_enabled_page(uint8_t page_mask)
{
    return (page_mask & all_work_page_mask()) != 0;
}

constexpr uint8_t kPageMaskV4KnownBits = static_cast<uint8_t>((1U << (kWorkPageHistory + 1)) - 1U);
constexpr uint8_t kPageMaskV5KnownBits = all_work_page_mask();
constexpr uint8_t kDefaultWorkPageMask = kPageMaskV5KnownBits;
constexpr uint8_t kWeatherBoardPageMask = work_page_mask_bit(kWorkPageWeatherBoard);
constexpr uint8_t kFlipClockPageMask = work_page_mask_bit(kWorkPageFlipClock);
constexpr EventBits_t kNetworkRequestClearBits = kProvisioningSyncBit |
                                                 kManualNtpSyncBit |
                                                 kManualWeatherSyncBit |
                                                 kManualSayingSyncBit |
                                                 kNetworkDiagBit |
                                                 kOtaCheckBit |
                                                 kOtaInstallBit;
static_assert((kNetworkRequestClearBits & kManualWeatherSyncBit) != 0,
              "network request clear bits must include manual weather sync");
static_assert((kNetworkRequestClearBits & kOtaCheckBit) != 0 &&
                  (kNetworkRequestClearBits & kOtaInstallBit) != 0,
              "network request clear bits must include OTA request bits");
constexpr const char *kConfigEventReasonFallback = "config";
constexpr const char *kConfigEventReasonNetworkRequestReset = "network request reset";
constexpr const char *kConfigEventReasonFactoryReset = "factory reset";
constexpr const char *kConfigEventReasonOfflineManualTime = "offline manual time";
constexpr const char *kConfigEventReasonProvisioningSave = "provisioning save";
constexpr const char *kConfigEventActionFallback = "action";
constexpr const char *kConfigEventActionClear = "clear";
constexpr const char *kConfigEventActionSet = "set";
constexpr const char *kNvsActionAccessingConfig = "accessing config";
constexpr const char *kNvsActionLoadingConfig = "loading config";
constexpr const char *kNvsActionSavingOfflineMode = "saving offline mode";
constexpr const char *kNvsActionSavingConfig = "saving config";
constexpr const char *kNvsActionSavingWeatherCity = "saving weather city";
constexpr const char *kNvsActionClearingWeatherCity = "clearing weather city";
constexpr const char *kNvsActionSavingHourlyReminder = "saving hourly reminder";
constexpr const char *kNvsActionSavingPageSettings = "saving page settings";
constexpr const char *kNvsActionSavingPageOrder = "saving page order";
constexpr const char *kNvsActionClearingConfig = "clearing config";
constexpr const char *kEmptyWifiSsidSaveLog = "skip saving empty wifi ssid";
constexpr const char *kInvalidWeatherCitySaveLog = "skip saving invalid weather city";
constexpr const char *kInvalidWeatherCityLoadLog = "ignore invalid weather city loaded from NVS";
#define CONFIG_EVENT_GROUP_UNAVAILABLE_FORMAT "skip %s event bits for %s: event group unavailable"
#define NVS_OPEN_FAILED_FORMAT "nvs open failed while %s: %s"
#define NVS_READ_U8_FAILED_FORMAT "nvs read u8 key=%s failed: %s"
#define NVS_SAVE_OFFLINE_MODE_FAILED_FORMAT "nvs save offline mode failed: %s"
#define NVS_ERASE_LEGACY_API_HOST_FAILED_FORMAT "nvs erase legacy api host failed while saving config: %s"
#define NVS_SAVE_CONFIG_FAILED_FORMAT "nvs save config failed: %s"
#define NVS_SAVE_WEATHER_CITY_FAILED_FORMAT "nvs save weather city failed: %s"
#define NVS_CLEAR_WEATHER_CITY_FAILED_FORMAT "nvs clear weather city failed: %s"
#define NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT "nvs save hourly reminder failed: %s"
#define NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT "nvs save page settings failed: %s"
#define NVS_SAVE_PAGE_ORDER_FAILED_FORMAT "nvs save page order failed: %s"
#define NVS_ERASE_KEY_CLEARING_CONFIG_FAILED_FORMAT "nvs erase key %s failed while clearing config: %s"
#define NVS_CLEAR_CONFIG_FAILED_FORMAT "nvs clear config failed: %s"
#define OFFLINE_SETUP_EMPTY_BODY_LOG "offline setup ignored empty request body"
#define OFFLINE_SETUP_INVALID_MANUAL_TIME_LOG "offline setup ignored invalid manual time"
#define MANUAL_TIME_MKTIME_FAILED_LOG "set manual offline time skipped: mktime failed"
#define MANUAL_TIME_SETTIMEOFDAY_FAILED_FORMAT "set manual offline time failed errno=%d"
#define OFFLINE_MODE_ENABLED_MANUAL_TIME_FORMAT "offline mode enabled with manual time: %04d-%02d-%02d %02d:%02d:%02d"
#define PROVISIONING_EMPTY_BODY_LOG "provisioning ignored empty request body"
#define PROVISIONING_EMPTY_SSID_LOG "provisioning ignored empty ssid"
#define PROVISIONING_EMPTY_API_KEY_LOG "provisioning ignored empty api key for online setup"
#define PROVISIONING_INVALID_WEATHER_CITY_LOG "provisioning ignored invalid weather city"
#define PROVISIONING_SAVED_FORMAT "provisioning saved ssid=%s pass_len=%u api_key=%s len=%u weather_city=%s city_len=%u"
constexpr const char *kFormManualTimeKey = "manual_time";
constexpr const char *kFormManualTimeFallbackKey = "datetime";
constexpr const char *kFormSsidKey = "ssid";
constexpr const char *kFormPasswordKey = "pass";
constexpr const char *kFormPasswordFallbackKey = "password";
constexpr const char *kFormApiKeyKey = "api_key";
constexpr const char *kFormApiKeyFallbackKey = "weather";
constexpr const char *kFormWeatherCityKey = "weather_city";
constexpr const char *kFormWeatherCityFallbackKey = "city";
constexpr const char *kInvalidWeatherCityChars = "&=?#%/\\<>\"'";
constexpr const char *kFormConfigTexts[] = {
    kFormManualTimeKey,
    kFormManualTimeFallbackKey,
    kFormSsidKey,
    kFormPasswordKey,
    kFormPasswordFallbackKey,
    kFormApiKeyKey,
    kFormApiKeyFallbackKey,
    kFormWeatherCityKey,
    kFormWeatherCityFallbackKey,
};
constexpr const char *kNvsConfigTexts[] = {
    kWifiNvsNamespace,
    kWifiSsidKey,
    kWifiPassKey,
    kWeatherApiKeyKey,
    kManualWeatherCityKey,
    kIgnoredAssetWeatherCityKey,
    kLegacyApiHostKey,
    kOfflineModeKey,
    kHourlyChimeKey,
    kHourlyAllDayKey,
    kChimeVolumeKey,
    kChimeSoundKey,
    kPageMaskV1Key,
    kPageMaskV2Key,
    kPageMaskV3Key,
    kPageMaskV4Key,
    kPageMaskV5Key,
    kPageOrderV1Key,
    kPageOrderV2Key,
    kPageOrderV3Key,
    kPageOrderV4Key,
    kPageOrderV5Key,
};
constexpr const char *kClearConfigKeys[] = {
    kWifiSsidKey,
    kWifiPassKey,
    kWeatherApiKeyKey,
    kManualWeatherCityKey,
    kIgnoredAssetWeatherCityKey,
    kLegacyApiHostKey,
    kOfflineModeKey,
    kPageMaskV1Key,
    kPageMaskV2Key,
    kPageMaskV3Key,
    kPageMaskV4Key,
    kPageMaskV5Key,
    kPageOrderV1Key,
    kPageOrderV2Key,
    kPageOrderV3Key,
    kPageOrderV4Key,
    kPageOrderV5Key,
};
constexpr const char *kConfigEventTexts[] = {
    kConfigEventReasonFallback,
    kConfigEventReasonNetworkRequestReset,
    kConfigEventReasonFactoryReset,
    kConfigEventReasonOfflineManualTime,
    kConfigEventReasonProvisioningSave,
    kConfigEventActionFallback,
    kConfigEventActionClear,
    kConfigEventActionSet,
};
constexpr const char *kNvsActionTexts[] = {
    kNvsActionAccessingConfig,
    kNvsActionLoadingConfig,
    kNvsActionSavingOfflineMode,
    kNvsActionSavingConfig,
    kNvsActionSavingWeatherCity,
    kNvsActionClearingWeatherCity,
    kNvsActionSavingHourlyReminder,
    kNvsActionSavingPageSettings,
    kNvsActionSavingPageOrder,
    kNvsActionClearingConfig,
};
constexpr const char *kConfigWarningTexts[] = {
    kEmptyWifiSsidSaveLog,
    kInvalidWeatherCitySaveLog,
    kInvalidWeatherCityLoadLog,
    NVS_READ_U8_FAILED_FORMAT,
};

struct ProvisioningFormFields {
    char ssid[kSetupSsidFieldSize] = {};
    char pass[kSetupPasswordFieldSize] = {};
    char api_key[kSetupApiKeyFieldSize] = {};
    char weather_city[kSetupWeatherCityFieldSize] = {};
};

struct LoadedNetworkConfig {
    esp_err_t ssid_err = ESP_FAIL;
    esp_err_t pass_err = ESP_FAIL;
    esp_err_t key_err = ESP_FAIL;
    uint8_t chime = 0;
    uint8_t all_day = 0;
    uint8_t volume = kDefaultChimeVolumePercent;
    uint8_t sound = 0;
    uint8_t page_mask = kPageMaskV5KnownBits;
    uint8_t offline = 0;
    uint8_t page_order[kWorkPageCount] = {};
    bool have_page_order = false;
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr bool cstr_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

constexpr const char *cstr_or_empty(const char *text)
{
    return text ? text : "";
}

constexpr bool output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *text : items) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

template <typename T, size_t N>
constexpr bool cstr_array_contains(const T (&items)[N], const char *needle)
{
    for (const char *text : items) {
        if (cstr_equal(text, needle)) {
            return true;
        }
    }
    return false;
}

constexpr bool valid_chime_volumes_include_default()
{
    for (uint8_t volume : kValidChimeVolumePercent) {
        if (volume == kDefaultChimeVolumePercent) {
            return true;
        }
    }
    return false;
}

constexpr bool valid_chime_volumes_ordered_and_bounded()
{
    uint8_t previous = 0;
    for (uint8_t volume : kValidChimeVolumePercent) {
        if (volume == 0 || volume > 100 || volume <= previous) {
            return false;
        }
        previous = volume;
    }
    return true;
}

constexpr bool clear_config_keys_nonempty()
{
    return cstr_array_nonempty(kClearConfigKeys);
}

constexpr bool nvs_config_texts_nonempty()
{
    return cstr_array_nonempty(kNvsConfigTexts);
}

constexpr bool form_config_texts_nonempty()
{
    return cstr_array_nonempty(kFormConfigTexts);
}

constexpr bool config_event_texts_nonempty()
{
    return cstr_array_nonempty(kConfigEventTexts);
}

constexpr bool nvs_action_texts_nonempty()
{
    return cstr_array_nonempty(kNvsActionTexts);
}

constexpr bool config_warning_texts_nonempty()
{
    return cstr_array_nonempty(kConfigWarningTexts);
}

constexpr bool cstr_contains_char(const char *text, char needle)
{
    if (!text) {
        return false;
    }
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor == needle) {
            return true;
        }
    }
    return false;
}

static_assert(kNetworkFormEncodedBufferSize >= kMaxSetupFormFieldSize,
              "form encoded scratch buffer must fit the largest setup field");
static_assert(kManualTimeFieldSize > 1, "manual time field must fit text and NUL");
static_assert(kSetupSsidFieldSize > 1, "setup SSID field must fit text and NUL");
static_assert(kSetupPasswordFieldSize > 1, "setup password field must fit text and NUL");
static_assert(kSetupApiKeyFieldSize > 1, "setup API key field must fit text and NUL");
static_assert(kSetupWeatherCityFieldSize == kManualWeatherCityLen,
              "setup weather city field must match runtime city buffer");
static_assert(array_count(kNvsConfigTexts) > 0, "NVS config text registry must not be empty");
static_assert(nvs_config_texts_nonempty(), "NVS namespace and config keys must be non-empty");
static_assert(array_count(kValidChimeVolumePercent) > 0, "valid chime volume list must not be empty");
static_assert(valid_chime_volumes_ordered_and_bounded(),
              "valid chime volumes must be ordered percentages in 1..100");
static_assert(valid_chime_volumes_include_default(), "default chime volume must be selectable");
static_assert(array_count(kClearConfigKeys) > 0, "clear config key list must not be empty");
static_assert(clear_config_keys_nonempty(), "clear config keys must be non-empty");
static_assert(cstr_array_contains(kClearConfigKeys, kWifiSsidKey), "factory reset must clear Wi-Fi SSID");
static_assert(cstr_array_contains(kClearConfigKeys, kWifiPassKey), "factory reset must clear Wi-Fi password");
static_assert(cstr_array_contains(kClearConfigKeys, kWeatherApiKeyKey), "factory reset must clear weather API key");
static_assert(cstr_array_contains(kClearConfigKeys, kManualWeatherCityKey),
              "factory reset must clear manual weather city");
static_assert(cstr_array_contains(kClearConfigKeys, kIgnoredAssetWeatherCityKey),
              "factory reset must clear ignored asset weather city");
static_assert(cstr_array_contains(kClearConfigKeys, kLegacyApiHostKey), "factory reset must clear legacy API host");
static_assert(cstr_array_contains(kClearConfigKeys, kOfflineModeKey), "factory reset must clear offline mode");
static_assert(array_count(kFormConfigTexts) > 0, "form config text registry must not be empty");
static_assert(form_config_texts_nonempty(), "setup form keys must be non-empty");
static_assert(cstr_nonempty(kInvalidWeatherCityChars), "invalid weather city character list must be non-empty");
static_assert(cstr_contains_char(kInvalidWeatherCityChars, '&') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '=') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '%') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '/') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '<') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '>') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '"') &&
                  cstr_contains_char(kInvalidWeatherCityChars, '\''),
              "manual weather city reject list must keep form, URL and HTML control characters");
static_assert(cstr_nonempty(kConfigEventReasonFallback), "config event fallback reason must be non-empty");
static_assert(cstr_nonempty(kConfigEventActionFallback), "config event fallback action must be non-empty");
static_assert(cstr_nonempty(kConfigEventActionClear), "config event clear action must be non-empty");
static_assert(cstr_nonempty(kConfigEventActionSet), "config event set action must be non-empty");
static_assert(array_count(kConfigEventTexts) > 0, "config event text registry must not be empty");
static_assert(array_count(kNvsActionTexts) > 0, "NVS action text registry must not be empty");
static_assert(array_count(kConfigWarningTexts) > 0, "configuration warning text registry must not be empty");
static_assert(config_event_texts_nonempty(), "config event reason/action texts must be non-empty");
static_assert(nvs_action_texts_nonempty(), "NVS action texts must be non-empty");
static_assert(config_warning_texts_nonempty(), "configuration warning texts must be non-empty");
static_assert(kWorkPageCount <= 8, "work page enabled mask is stored as uint8_t");
static_assert((kPageMaskV4KnownBits & work_page_mask_bit(kWorkPageXiaozhiAI)) == 0,
              "page mask v4 must not include Xiaozhi AI page");
static_assert(kPageMaskV5KnownBits == all_work_page_mask(),
              "page mask v5 must cover every current work page");
static_assert((kDefaultWorkPageMask & kPageMaskV5KnownBits) == kDefaultWorkPageMask &&
                  work_page_mask_has_enabled_page(kDefaultWorkPageMask),
              "default work page mask must enable at least one known page");
static_assert((kPageMaskV5KnownBits & kWeatherBoardPageMask) == kWeatherBoardPageMask,
              "weather board page must be covered by the current page mask");
static_assert((kPageMaskV5KnownBits & kFlipClockPageMask) == kFlipClockPageMask,
              "flip clock page must be covered by the current page mask");

const char *config_event_reason_text(const char *reason)
{
    return cstr_nonempty(reason) ? reason : kConfigEventReasonFallback;
}

const char *config_event_action_text(const char *action)
{
    return cstr_nonempty(action) ? action : kConfigEventActionFallback;
}

const char *nvs_action_text(const char *action)
{
    return cstr_nonempty(action) ? action : kNvsActionAccessingConfig;
}

void log_config_event_group_unavailable(const char *action, const char *reason)
{
    ESP_LOGW(TAG, CONFIG_EVENT_GROUP_UNAVAILABLE_FORMAT,
             config_event_action_text(action),
             config_event_reason_text(reason));
}

void clear_app_event_bits(EventBits_t bits, const char *reason)
{
    if (!g_app_events) {
        log_config_event_group_unavailable(kConfigEventActionClear, reason);
        return;
    }
    xEventGroupClearBits(g_app_events, bits);
}

void set_app_event_bits(EventBits_t bits, const char *reason)
{
    if (!g_app_events) {
        log_config_event_group_unavailable(kConfigEventActionSet, reason);
        return;
    }
    xEventGroupSetBits(g_app_events, bits);
}

uint8_t normalize_chime_volume(uint8_t volume)
{
    for (uint8_t valid : kValidChimeVolumePercent) {
        if (volume == valid) {
            return volume;
        }
    }
    return kDefaultChimeVolumePercent;
}

uint8_t normalize_chime_sound_index(uint8_t sound)
{
    return sound < kChimeSoundCount ? sound : 0;
}

constexpr uint8_t bool_to_nvs_u8(bool value)
{
    return value ? 1 : 0;
}

constexpr bool nvs_u8_to_bool(uint8_t value)
{
    return value != 0;
}

esp_err_t erase_nvs_key_if_present(nvs_handle_t nvs, const char *key, bool *erased)
{
    esp_err_t err = nvs_erase_key(nvs, key);
    if (err == ESP_OK) {
        if (erased) {
            *erased = true;
        }
        return ESP_OK;
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t open_wifi_nvs(nvs_open_mode_t mode, nvs_handle_t *nvs, const char *action, bool log_not_found = true)
{
    if (!nvs) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_open(kWifiNvsNamespace, mode, nvs);
    if (err != ESP_OK && (log_not_found || err != ESP_ERR_NVS_NOT_FOUND)) {
        ESP_LOGW(TAG, NVS_OPEN_FAILED_FORMAT, nvs_action_text(action), esp_err_to_name(err));
    }
    return err;
}

esp_err_t commit_nvs_if_ok(nvs_handle_t nvs, esp_err_t err)
{
    return err == ESP_OK ? nvs_commit(nvs) : err;
}

esp_err_t commit_nvs_if_changed(nvs_handle_t nvs, esp_err_t err, bool changed)
{
    return changed ? commit_nvs_if_ok(nvs, err) : err;
}

esp_err_t set_nvs_str_if_ok(nvs_handle_t nvs, esp_err_t err, const char *key, const char *value)
{
    if (err != ESP_OK) {
        return err;
    }
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_str(nvs, key, value);
}

esp_err_t set_nvs_u8_if_ok(nvs_handle_t nvs, esp_err_t err, const char *key, uint8_t value)
{
    if (err != ESP_OK) {
        return err;
    }
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_u8(nvs, key, value);
}

esp_err_t write_optional_nvs_string_key(nvs_handle_t nvs, const char *key, const char *value)
{
    return cstr_nonempty(value)
               ? nvs_set_str(nvs, key, value)
               : erase_nvs_key_if_present(nvs, key, nullptr);
}

esp_err_t write_manual_weather_city_key(nvs_handle_t nvs, const char *city)
{
    return write_optional_nvs_string_key(nvs, kManualWeatherCityKey, city);
}

esp_err_t read_nvs_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len)
{
    if (!key || !output_buffer_available(out, out_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = out_len;
    return nvs_get_str(nvs, key, out, &len);
}

bool nvs_string_matches(nvs_handle_t nvs, const char *key, const char *expected, char *scratch, size_t scratch_len)
{
    if (!cstr_nonempty(expected) || !output_buffer_available(scratch, scratch_len)) {
        return false;
    }
    scratch[0] = '\0';
    return read_nvs_string(nvs, key, scratch, scratch_len) == ESP_OK &&
           strcmp(scratch, expected) == 0;
}

bool asset_weather_city_ignored(nvs_handle_t nvs, const char *city)
{
    if (!city || city[0] == '\0') {
        return false;
    }
    char ignored[kManualWeatherCityLen] = {};
    return nvs_string_matches(nvs, kIgnoredAssetWeatherCityKey, city, ignored, sizeof(ignored));
}

esp_err_t write_ignored_asset_weather_city(nvs_handle_t nvs, const char *city)
{
    return write_optional_nvs_string_key(nvs, kIgnoredAssetWeatherCityKey, city);
}

bool read_valid_asset_weather_city(char *out, size_t out_len)
{
    if (!output_buffer_available(out, out_len)) {
        return false;
    }
    out[0] = '\0';
    return custom_assets_read_weather_city(out, out_len) && is_weather_city_input_valid(out);
}

bool read_unignored_asset_weather_city(nvs_handle_t nvs, char *out, size_t out_len)
{
    return read_valid_asset_weather_city(out, out_len) && !asset_weather_city_ignored(nvs, out);
}

esp_err_t write_current_asset_weather_city_ignore(nvs_handle_t nvs)
{
    char asset_weather_city[kManualWeatherCityLen] = {};
    return read_valid_asset_weather_city(asset_weather_city, sizeof(asset_weather_city))
               ? write_ignored_asset_weather_city(nvs, asset_weather_city)
               : write_ignored_asset_weather_city(nvs, nullptr);
}

esp_err_t write_matching_asset_weather_city_ignore(nvs_handle_t nvs, const char *city, bool *wrote)
{
    if (wrote) {
        *wrote = false;
    }
    char asset_weather_city[kManualWeatherCityLen] = {};
    if (city && read_valid_asset_weather_city(asset_weather_city, sizeof(asset_weather_city)) &&
        strcmp(asset_weather_city, city) == 0) {
        if (wrote) {
            *wrote = true;
        }
        return write_ignored_asset_weather_city(nvs, asset_weather_city);
    }
    return ESP_OK;
}

uint8_t read_nvs_u8_or_default(nvs_handle_t nvs, const char *key, uint8_t default_value)
{
    if (!key) {
        return default_value;
    }
    uint8_t value = default_value;
    esp_err_t err = nvs_get_u8(nvs, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, NVS_READ_U8_FAILED_FORMAT, key, esp_err_to_name(err));
    }
    return value;
}

bool nvs_u8_matches(nvs_handle_t nvs, const char *key, uint8_t expected)
{
    if (!key) {
        return false;
    }
    uint8_t value = kNvsUnsetU8;
    return nvs_get_u8(nvs, key, &value) == ESP_OK && value == expected;
}

esp_err_t write_changed_nvs_u8(nvs_handle_t nvs, esp_err_t err, const char *key, uint8_t value, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (nvs_u8_matches(nvs, key, value)) {
        return ESP_OK;
    }
    if (changed) {
        *changed = true;
    }
    return set_nvs_u8_if_ok(nvs, err, key, value);
}

bool close_nvs_save_ok(nvs_handle_t nvs, esp_err_t err)
{
    nvs_close(nvs);
    return err == ESP_OK;
}

bool hourly_chime_settings_match_nvs(nvs_handle_t nvs,
                                     uint8_t chime,
                                     uint8_t all_day,
                                     uint8_t volume,
                                     uint8_t sound)
{
    uint8_t old_chime = 0;
    uint8_t old_all_day = 0;
    uint8_t old_volume = 0;
    uint8_t old_sound = 0;
    return nvs_get_u8(nvs, kHourlyChimeKey, &old_chime) == ESP_OK &&
           nvs_get_u8(nvs, kHourlyAllDayKey, &old_all_day) == ESP_OK &&
           nvs_get_u8(nvs, kChimeVolumeKey, &old_volume) == ESP_OK &&
           nvs_get_u8(nvs, kChimeSoundKey, &old_sound) == ESP_OK &&
           old_chime == chime &&
           old_all_day == all_day &&
           old_volume == volume &&
           old_sound == sound;
}

esp_err_t write_hourly_chime_settings_nvs(nvs_handle_t nvs,
                                          esp_err_t err,
                                          uint8_t chime,
                                          uint8_t all_day,
                                          uint8_t volume,
                                          uint8_t sound,
                                          bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (hourly_chime_settings_match_nvs(nvs, chime, all_day, volume, sound)) {
        return ESP_OK;
    }
    if (changed) {
        *changed = true;
    }
    err = set_nvs_u8_if_ok(nvs, err, kHourlyChimeKey, chime);
    err = set_nvs_u8_if_ok(nvs, err, kHourlyAllDayKey, all_day);
    err = set_nvs_u8_if_ok(nvs, err, kChimeVolumeKey, volume);
    err = set_nvs_u8_if_ok(nvs, err, kChimeSoundKey, sound);
    return err;
}

bool manual_weather_city_matches_nvs(nvs_handle_t nvs, const char *city)
{
    char saved_city[kManualWeatherCityLen] = {};
    return nvs_string_matches(nvs, kManualWeatherCityKey, city, saved_city, sizeof(saved_city));
}

uint8_t normalize_work_page_mask(uint8_t page_mask)
{
    page_mask &= kPageMaskV5KnownBits;
    return work_page_mask_has_enabled_page(page_mask) ? page_mask : kDefaultWorkPageMask;
}

uint8_t read_saved_page_mask(nvs_handle_t nvs)
{
    uint8_t page_mask = kDefaultWorkPageMask;
    if (nvs_get_u8(nvs, kPageMaskV5Key, &page_mask) == ESP_OK) {
        return normalize_work_page_mask(page_mask);
    }
    if (nvs_get_u8(nvs, kPageMaskV4Key, &page_mask) == ESP_OK) {
        return normalize_work_page_mask(static_cast<uint8_t>(page_mask | work_page_mask_bit(kWorkPageXiaozhiAI)));
    }
    return page_mask;
}

bool read_saved_page_order(nvs_handle_t nvs, uint8_t *page_order, size_t page_order_size)
{
    if (!page_order || page_order_size != kWorkPageCount) {
        return false;
    }
    size_t stored_len = page_order_size;
    if (nvs_get_blob(nvs, kPageOrderV5Key, page_order, &stored_len) == ESP_OK &&
        stored_len == page_order_size) {
        return true;
    }
    uint8_t legacy_order[kWorkPageHistory + 1] = {};
    stored_len = sizeof(legacy_order);
    if (nvs_get_blob(nvs, kPageOrderV4Key, legacy_order, &stored_len) == ESP_OK &&
        stored_len == sizeof(legacy_order)) {
        memcpy(page_order, legacy_order, sizeof(legacy_order));
        page_order[kWorkPageXiaozhiAI] = kWorkPageXiaozhiAI;
        return true;
    }
    return false;
}

bool saved_page_order_matches(nvs_handle_t nvs, const uint8_t *page_order, size_t page_order_size)
{
    uint8_t saved_order[kWorkPageCount] = {};
    return page_order &&
           page_order_size == sizeof(saved_order) &&
           read_saved_page_order(nvs, saved_order, sizeof(saved_order)) &&
           memcmp(saved_order, page_order, page_order_size) == 0;
}

esp_err_t write_work_page_order_nvs(nvs_handle_t nvs,
                                    esp_err_t err,
                                    const uint8_t *page_order,
                                    size_t page_order_size,
                                    bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (saved_page_order_matches(nvs, page_order, page_order_size)) {
        return ESP_OK;
    }
    if (changed) {
        *changed = true;
    }
    return nvs_set_blob(nvs, kPageOrderV5Key, page_order, page_order_size);
}

esp_err_t erase_saved_config_keys(nvs_handle_t nvs)
{
    bool erased = false;
    esp_err_t err = ESP_OK;
    for (const char *key : kClearConfigKeys) {
        bool key_erased = false;
        err = erase_nvs_key_if_present(nvs, key, &key_erased);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, NVS_ERASE_KEY_CLEARING_CONFIG_FAILED_FORMAT, key, esp_err_to_name(err));
            break;
        }
        erased = erased || key_erased;
    }
    return commit_nvs_if_changed(nvs, err, erased);
}

bool clear_saved_config_nvs()
{
    nvs_handle_t nvs;
    esp_err_t open_err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionClearingConfig);
    if (open_err != ESP_OK) {
        return false;
    }
    esp_err_t err = erase_saved_config_keys(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, NVS_CLEAR_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

void load_saved_manual_weather_city(nvs_handle_t nvs)
{
    esp_err_t city_err = read_nvs_string(nvs,
                                         kManualWeatherCityKey,
                                         g_manual_weather_city,
                                         sizeof(g_manual_weather_city));
    if (city_err == ESP_OK) {
        trim_ascii(g_manual_weather_city);
        if (!is_weather_city_input_valid(g_manual_weather_city)) {
            ESP_LOGW(TAG, "%s", kInvalidWeatherCityLoadLog);
            g_manual_weather_city[0] = '\0';
        }
    } else {
        g_manual_weather_city[0] = '\0';
    }
    if (g_manual_weather_city[0] == '\0') {
        char asset_weather_city[kManualWeatherCityLen] = {};
        if (read_unignored_asset_weather_city(nvs, asset_weather_city, sizeof(asset_weather_city))) {
            strlcpy(g_manual_weather_city, asset_weather_city, sizeof(g_manual_weather_city));
        }
    }
}

void apply_loaded_page_config(uint8_t page_mask, const uint8_t *page_order, bool have_page_order)
{
    g_work_page_enabled_mask = normalize_work_page_mask(page_mask);
    if (have_page_order && page_order) {
        memcpy(g_work_page_order, page_order, sizeof(g_work_page_order));
    }
    normalize_work_page_order();
    g_active_work_page = first_enabled_work_page();
}

LoadedNetworkConfig read_loaded_network_config(nvs_handle_t nvs)
{
    LoadedNetworkConfig loaded = {};
    loaded.ssid_err = read_nvs_string(nvs, kWifiSsidKey, g_wifi_ssid, sizeof(g_wifi_ssid));
    loaded.pass_err = read_nvs_string(nvs, kWifiPassKey, g_wifi_pass, sizeof(g_wifi_pass));
    loaded.key_err = read_nvs_string(nvs, kWeatherApiKeyKey, g_weather_api_key, sizeof(g_weather_api_key));
    loaded.chime = read_nvs_u8_or_default(nvs, kHourlyChimeKey, 0);
    loaded.all_day = read_nvs_u8_or_default(nvs, kHourlyAllDayKey, 0);
    loaded.volume = read_nvs_u8_or_default(nvs, kChimeVolumeKey, kDefaultChimeVolumePercent);
    loaded.sound = read_nvs_u8_or_default(nvs, kChimeSoundKey, 0);
    loaded.page_mask = read_saved_page_mask(nvs);
    loaded.offline = read_nvs_u8_or_default(nvs, kOfflineModeKey, 0);
    load_saved_manual_weather_city(nvs);
    loaded.have_page_order = read_saved_page_order(nvs, loaded.page_order, sizeof(loaded.page_order));
    return loaded;
}

void apply_loaded_network_config(const LoadedNetworkConfig &loaded)
{
    g_have_weather_key = loaded.key_err == ESP_OK && g_weather_api_key[0] != '\0';
    g_has_manual_weather_city = g_manual_weather_city[0] != '\0';
    g_hourly_chime_enabled = nvs_u8_to_bool(loaded.chime);
    g_hourly_chime_all_day = nvs_u8_to_bool(loaded.all_day);
    g_offline_mode_ui_enabled = nvs_u8_to_bool(loaded.offline);
    g_chime_volume_percent = normalize_chime_volume(loaded.volume);
    g_chime_sound_index = normalize_chime_sound_index(loaded.sound);
    apply_loaded_page_config(loaded.page_mask, loaded.page_order, loaded.have_page_order);
}
} // namespace

bool load_saved_config()
{
    nvs_handle_t nvs;
    esp_err_t open_err = open_wifi_nvs(NVS_READONLY, &nvs, kNvsActionLoadingConfig, false);
    if (open_err != ESP_OK) {
        return false;
    }
    LoadedNetworkConfig loaded = read_loaded_network_config(nvs);
    nvs_close(nvs);
    apply_loaded_network_config(loaded);
    return loaded.ssid_err == ESP_OK && loaded.pass_err == ESP_OK && g_wifi_ssid[0] != '\0';
}

void clear_network_request_bits()
{
    clear_app_event_bits(kNetworkRequestClearBits, kConfigEventReasonNetworkRequestReset);
}

bool set_offline_mode_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingOfflineMode);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t next_value = bool_to_nvs_u8(enabled);
    bool changed = false;
    err = write_changed_nvs_u8(nvs, err, kOfflineModeKey, next_value, &changed);
    err = commit_nvs_if_changed(nvs, err, changed);
    if (!close_nvs_save_ok(nvs, err)) {
        ESP_LOGW(TAG, NVS_SAVE_OFFLINE_MODE_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    g_offline_mode_ui_enabled = enabled;
    if (enabled) {
        clear_network_request_bits();
        if (!g_setup_portal_active) {
            stop_wifi_radio(true);
        }
    }
    return true;
}

bool can_leave_offline_mode_without_setup()
{
    return g_have_wifi_creds && g_have_weather_key;
}

bool weather_city_char_invalid(unsigned char ch)
{
    return ch < 0x20 || strchr(kInvalidWeatherCityChars, ch) != nullptr;
}

bool is_weather_city_input_valid(const char *city)
{
    if (!city || city[0] == '\0') {
        return true;
    }
    size_t len = strlen(city);
    if (len >= kManualWeatherCityLen) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (weather_city_char_invalid((unsigned char)city[i])) {
            return false;
        }
    }
    return true;
}

void copy_trimmed_weather_city(char *out, size_t out_len, const char *city)
{
    if (!output_buffer_available(out, out_len)) {
        return;
    }
    out[0] = '\0';
    if (!city) {
        return;
    }
    strlcpy(out, city, out_len);
    trim_ascii(out);
}

void set_manual_weather_city_state(const char *city)
{
    strlcpy(g_manual_weather_city, cstr_or_empty(city), sizeof(g_manual_weather_city));
    g_has_manual_weather_city = g_manual_weather_city[0] != '\0';
}

bool finish_manual_weather_city_save(nvs_handle_t nvs,
                                     esp_err_t err,
                                     const char *city,
                                     bool changed)
{
    err = commit_nvs_if_changed(nvs, err, changed);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, NVS_SAVE_WEATHER_CITY_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    set_manual_weather_city_state(city);
    return true;
}

esp_err_t write_manual_weather_city_save_nvs(nvs_handle_t nvs,
                                             const char *city,
                                             bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (manual_weather_city_matches_nvs(nvs, city)) {
        return erase_nvs_key_if_present(nvs, kIgnoredAssetWeatherCityKey, changed);
    }
    if (changed) {
        *changed = true;
    }
    esp_err_t err = write_manual_weather_city_key(nvs, city);
    if (err == ESP_OK) {
        err = write_ignored_asset_weather_city(nvs, nullptr);
    }
    return err;
}

esp_err_t clear_manual_weather_city_nvs(nvs_handle_t nvs, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    bool erased = false;
    esp_err_t err = erase_nvs_key_if_present(nvs, kManualWeatherCityKey, &erased);
    if (err == ESP_OK) {
        bool ignored_asset_city = false;
        err = write_matching_asset_weather_city_ignore(nvs, g_manual_weather_city, &ignored_asset_city);
        erased = erased || ignored_asset_city;
    }
    if (changed) {
        *changed = erased;
    }
    return err;
}

void reset_saved_config_runtime_state()
{
    g_wifi_ssid[0] = '\0';
    g_wifi_pass[0] = '\0';
    g_weather_api_key[0] = '\0';
    set_manual_weather_city_state("");
    g_sta_ip[0] = '\0';
    g_have_wifi_creds = false;
    g_have_weather_key = false;
    g_offline_mode_ui_enabled = false;
    clear_app_event_bits(kWifiConnectedBit | kWeatherReadyBit, kConfigEventReasonFactoryReset);
    clear_network_request_bits();
}

void apply_saved_config_runtime_state(const char *ssid,
                                      const char *pass,
                                      const char *api_key,
                                      const char *weather_city)
{
    strlcpy(g_wifi_ssid, cstr_or_empty(ssid), sizeof(g_wifi_ssid));
    strlcpy(g_wifi_pass, cstr_or_empty(pass), sizeof(g_wifi_pass));
    strlcpy(g_weather_api_key, cstr_or_empty(api_key), sizeof(g_weather_api_key));
    set_manual_weather_city_state(weather_city);
    g_have_wifi_creds = g_wifi_ssid[0] != '\0';
    g_have_weather_key = g_weather_api_key[0] != '\0';
}

esp_err_t write_saved_config_nvs(nvs_handle_t nvs,
                                 const char *ssid,
                                 const char *pass,
                                 const char *api_key,
                                 const char *city)
{
    esp_err_t err = set_nvs_str_if_ok(nvs, ESP_OK, kWifiSsidKey, ssid);
    err = set_nvs_str_if_ok(nvs, err, kWifiPassKey, pass);
    err = set_nvs_str_if_ok(nvs, err, kWeatherApiKeyKey, api_key);
    if (err == ESP_OK) {
        err = write_manual_weather_city_key(nvs, city);
    }
    if (err == ESP_OK) {
        err = city && city[0] != '\0'
                  ? write_ignored_asset_weather_city(nvs, nullptr)
                  : write_current_asset_weather_city_ignore(nvs);
    }
    // Provisioning credentials are only useful after leaving offline mode. Keep both
    // changes in this transaction so a later NVS write cannot leave them out of sync.
    err = set_nvs_u8_if_ok(nvs, err, kOfflineModeKey, 0);
    esp_err_t legacy_erase_err = erase_nvs_key_if_present(nvs, kLegacyApiHostKey, nullptr);
    if (legacy_erase_err != ESP_OK) {
        ESP_LOGW(TAG, NVS_ERASE_LEGACY_API_HOST_FAILED_FORMAT,
                 esp_err_to_name(legacy_erase_err));
    }
    return err;
}

bool save_config(const char *ssid, const char *pass, const char *api_key, const char *weather_city)
{
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGW(TAG, "%s", kEmptyWifiSsidSaveLog);
        return false;
    }
    if (!pass) {
        pass = "";
    }
    if (!api_key) {
        api_key = "";
    }
    char city[kManualWeatherCityLen] = {};
    copy_trimmed_weather_city(city, sizeof(city), weather_city);
    if (!is_weather_city_input_valid(city)) {
        ESP_LOGW(TAG, "%s", kInvalidWeatherCitySaveLog);
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingConfig);
    if (err != ESP_OK) {
        return false;
    }
    err = write_saved_config_nvs(nvs, ssid, pass, api_key, city);
    err = commit_nvs_if_ok(nvs, err);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, NVS_SAVE_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    apply_saved_config_runtime_state(ssid, pass, api_key, city);
    g_offline_mode_ui_enabled = false;
    return true;
}

bool save_manual_weather_city(const char *city)
{
    char next[kManualWeatherCityLen] = {};
    copy_trimmed_weather_city(next, sizeof(next), city);
    if (next[0] == '\0') {
        return clear_manual_weather_city();
    }
    if (!is_weather_city_input_valid(next)) {
        ESP_LOGW(TAG, "%s", kInvalidWeatherCitySaveLog);
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingWeatherCity);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_manual_weather_city_save_nvs(nvs, next, &changed);
    return finish_manual_weather_city_save(nvs, err, next, changed);
}

bool clear_manual_weather_city()
{
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionClearingWeatherCity);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = clear_manual_weather_city_nvs(nvs, &changed);
    err = commit_nvs_if_changed(nvs, err, changed);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, NVS_CLEAR_WEATHER_CITY_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    set_manual_weather_city_state("");
    return true;
}

bool save_hourly_chime_setting()
{
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingHourlyReminder);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t next_chime = bool_to_nvs_u8(g_hourly_chime_enabled);
    uint8_t next_all_day = bool_to_nvs_u8(g_hourly_chime_all_day);
    uint8_t next_volume = (uint8_t)g_chime_volume_percent;
    uint8_t next_sound = (uint8_t)g_chime_sound_index;
    bool changed = false;
    err = write_hourly_chime_settings_nvs(nvs, err, next_chime, next_all_day, next_volume, next_sound, &changed);
    err = commit_nvs_if_changed(nvs, err, changed);
    if (!close_nvs_save_ok(nvs, err)) {
        ESP_LOGW(TAG, NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_work_page_settings()
{
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingPageSettings);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t mask = normalize_work_page_mask(g_work_page_enabled_mask);
    bool changed = false;
    err = write_changed_nvs_u8(nvs, err, kPageMaskV5Key, mask, &changed);
    err = commit_nvs_if_changed(nvs, err, changed);
    if (!close_nvs_save_ok(nvs, err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    g_work_page_enabled_mask = mask;
    return true;
}

bool save_work_page_order()
{
    normalize_work_page_order();
    nvs_handle_t nvs;
    esp_err_t err = open_wifi_nvs(NVS_READWRITE, &nvs, kNvsActionSavingPageOrder);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_work_page_order_nvs(nvs, err, g_work_page_order, sizeof(g_work_page_order), &changed);
    err = commit_nvs_if_changed(nvs, err, changed);
    if (!close_nvs_save_ok(nvs, err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_ORDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool clear_saved_config()
{
    if (!clear_saved_config_nvs()) {
        return false;
    }
    xiaozhi_ai_clear_activation();
    reset_saved_config_runtime_state();
    return true;
}

void form_value_fallback_trimmed(const char *body, const char *primary_key, const char *fallback_key, char *out, size_t out_len)
{
    form_value_fallback(body, primary_key, fallback_key, out, out_len);
    trim_ascii(out);
}

void read_provisioning_form_fields(const char *body, ProvisioningFormFields *fields)
{
    if (!fields) {
        return;
    }
    form_value(body, kFormSsidKey, fields->ssid, sizeof(fields->ssid));
    form_value_fallback(body, kFormPasswordKey, kFormPasswordFallbackKey, fields->pass, sizeof(fields->pass));
    form_value_fallback_trimmed(body,
                                kFormApiKeyKey,
                                kFormApiKeyFallbackKey,
                                fields->api_key,
                                sizeof(fields->api_key));
    form_value_fallback_trimmed(body,
                                kFormWeatherCityKey,
                                kFormWeatherCityFallbackKey,
                                fields->weather_city,
                                sizeof(fields->weather_city));
}

bool save_offline_datetime_from_body(const char *body)
{
    if (!body) {
        ESP_LOGW(TAG, "%s", OFFLINE_SETUP_EMPTY_BODY_LOG);
        return false;
    }
    char manual_time[kManualTimeFieldSize] = {};
    form_value_fallback_trimmed(body, kFormManualTimeKey, kFormManualTimeFallbackKey, manual_time, sizeof(manual_time));
    struct tm local = {};
    if (!parse_manual_datetime_text(manual_time, &local)) {
        ESP_LOGW(TAG, "%s", OFFLINE_SETUP_INVALID_MANUAL_TIME_LOG);
        return false;
    }
    time_t epoch = mktime(&local);
    if (epoch <= 0) {
        ESP_LOGW(TAG, "%s", MANUAL_TIME_MKTIME_FAILED_LOG);
        return false;
    }
    struct timeval now = {};
    now.tv_sec = epoch;
    if (settimeofday(&now, nullptr) != 0) {
        ESP_LOGW(TAG, MANUAL_TIME_SETTIMEOFDAY_FAILED_FORMAT, errno);
        return false;
    }
    sync_rtc_from_system_time();
    if (!set_offline_mode_enabled(true)) {
        return false;
    }
    set_app_event_bits(kTimeSyncedBit, kConfigEventReasonOfflineManualTime);
    ESP_LOGI(TAG, OFFLINE_MODE_ENABLED_MANUAL_TIME_FORMAT,
             local.tm_year + kManualTimeTmYearOffset,
             local.tm_mon + kManualTimeTmMonthOffset,
             local.tm_mday,
             local.tm_hour,
             local.tm_min,
             local.tm_sec);
    return true;
}

bool save_credentials_from_body(const char *body)
{
    if (!body) {
        ESP_LOGW(TAG, "%s", PROVISIONING_EMPTY_BODY_LOG);
        return false;
    }
    ProvisioningFormFields fields = {};
    read_provisioning_form_fields(body, &fields);
    if (fields.ssid[0] == '\0') {
        ESP_LOGW(TAG, "%s", PROVISIONING_EMPTY_SSID_LOG);
        return false;
    }
    if (fields.api_key[0] == '\0' && g_weather_api_key[0] != '\0') {
        strlcpy(fields.api_key, g_weather_api_key, sizeof(fields.api_key));
    }
    if (fields.api_key[0] == '\0') {
        ESP_LOGW(TAG, "%s", PROVISIONING_EMPTY_API_KEY_LOG);
        return false;
    }
    if (!is_weather_city_input_valid(fields.weather_city)) {
        ESP_LOGW(TAG, "%s", PROVISIONING_INVALID_WEATHER_CITY_LOG);
        return false;
    }
    ESP_LOGI(TAG, PROVISIONING_SAVED_FORMAT,
             fields.ssid,
             (unsigned)strlen(fields.pass),
             fields.api_key[0] ? "set" : "empty",
             (unsigned)strlen(fields.api_key),
             fields.weather_city[0] ? "set" : "auto",
             (unsigned)strlen(fields.weather_city));
    g_last_wifi_disconnect_reason = 0;
    clear_app_event_bits(kWifiConnectedBit, kConfigEventReasonProvisioningSave);
    if (!save_config(fields.ssid, fields.pass, fields.api_key, fields.weather_city)) {
        return false;
    }
    (void)apply_station_config(true);
    return true;
}
