// 负责 Wi-Fi、API Key、页面设置和声音设置的 NVS 配置读写。
#include "network_services.h"

#include "app_constexpr.h"
#include "app_text_format.h"
#include "alarm_services.h"
#include "chime_settings.h"
#include "network_config_nvs.h"
#include "network_config_internal.h"
#include "weather_city_text.h"

#include "custom_assets.h"
#include "ui_views.h"

using network_config_nvs::commit_nvs_if_changed;
using network_config_nvs::commit_nvs_if_ok;
using network_config_nvs::erase_nvs_key_if_present;
using network_config_nvs::nvs_string_matches;
using network_config_nvs::nvs_u8_matches;
using network_config_nvs::read_nvs_string;
using network_config_nvs::read_nvs_u8_or_default;
using network_config_nvs::ScopedNvsHandle;
using network_config_nvs::set_nvs_str_if_ok;
using network_config_nvs::set_nvs_u8_if_ok;
using network_config_nvs::write_changed_nvs_u8;
using network_config_nvs::write_optional_nvs_string_key;

namespace {
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
constexpr const char *kXiaozhiAutoReturnKey = "xz_auto_ret_v1";
constexpr uint8_t work_page_mask_bit(int page)
{
    return static_cast<uint8_t>(1U << page);
}

constexpr uint8_t all_work_page_mask()
{
    return static_cast<uint8_t>((1U << kWorkPageCount) - 1);
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
constexpr const char *kConfigEventReasonNetworkRequestReset = "network request reset";
constexpr const char *kConfigEventReasonFactoryReset = "factory reset";
constexpr const char *kNvsActionLoadingConfig = "loading config";
constexpr const char *kNvsActionSavingOfflineMode = "saving offline mode";
constexpr const char *kNvsActionSavingConfig = "saving config";
constexpr const char *kNvsActionSavingWeatherCity = "saving weather city";
constexpr const char *kNvsActionClearingWeatherCity = "clearing weather city";
constexpr const char *kNvsActionSavingHourlyReminder = "saving hourly reminder";
constexpr const char *kNvsActionSavingPageSettings = "saving page settings";
constexpr const char *kNvsActionSavingPageOrder = "saving page order";
constexpr const char *kNvsActionSavingXiaozhiAutoReturn = "saving Xiaozhi auto return";
constexpr const char *kNvsActionClearingConfig = "clearing config";
constexpr const char *kEmptyWifiSsidSaveLog = "skip saving empty wifi ssid";
constexpr const char *kInvalidWeatherCitySaveLog = "skip saving invalid weather city";
constexpr const char *kInvalidWeatherCityLoadLog = "ignore invalid weather city loaded from NVS";
constexpr const char *kOfflinePageMaskPersistFailedLog = "failed to persist offline-compatible page settings";
#define NVS_SAVE_OFFLINE_MODE_FAILED_FORMAT "nvs save offline mode failed: %s"
#define NVS_ERASE_LEGACY_API_HOST_FAILED_FORMAT "nvs erase legacy api host failed while saving config: %s"
#define NVS_SAVE_CONFIG_FAILED_FORMAT "nvs save config failed: %s"
#define NVS_SAVE_WEATHER_CITY_FAILED_FORMAT "nvs save weather city failed: %s"
#define NVS_CLEAR_WEATHER_CITY_FAILED_FORMAT "nvs clear weather city failed: %s"
#define NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT "nvs save hourly reminder failed: %s"
#define NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT "nvs save page settings failed: %s"
#define NVS_SAVE_PAGE_ORDER_FAILED_FORMAT "nvs save page order failed: %s"
#define NVS_SAVE_XIAOZHI_AUTO_RETURN_FAILED_FORMAT "nvs save Xiaozhi auto return failed: %s"
#define NVS_ERASE_KEY_CLEARING_CONFIG_FAILED_FORMAT "nvs erase key %s failed while clearing config: %s"
#define NVS_CLEAR_CONFIG_FAILED_FORMAT "nvs clear config failed: %s"
constexpr const char *kSavedConfigKeys[] = {
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
    kXiaozhiAutoReturnKey,
};
constexpr const char *kConfigEventReasonTexts[] = {
    kConfigEventReasonNetworkRequestReset,
    kConfigEventReasonFactoryReset,
};
constexpr const char *kNvsActionTexts[] = {
    kNvsActionLoadingConfig,
    kNvsActionSavingOfflineMode,
    kNvsActionSavingConfig,
    kNvsActionSavingWeatherCity,
    kNvsActionClearingWeatherCity,
    kNvsActionSavingHourlyReminder,
    kNvsActionSavingPageSettings,
    kNvsActionSavingPageOrder,
    kNvsActionSavingXiaozhiAutoReturn,
    kNvsActionClearingConfig,
};
constexpr const char *kConfigWarningTexts[] = {
    kEmptyWifiSsidSaveLog,
    kInvalidWeatherCitySaveLog,
    kInvalidWeatherCityLoadLog,
    kOfflinePageMaskPersistFailedLog,
};

struct LoadedNetworkConfig {
    esp_err_t ssid_err = ESP_FAIL;
    esp_err_t pass_err = ESP_FAIL;
    esp_err_t key_err = ESP_FAIL;
    uint8_t chime = 0;
    uint8_t all_day = 0;
    uint8_t volume = chime_settings::kDefaultVolumePercent;
    uint8_t sound = 0;
    uint8_t page_mask = kPageMaskV5KnownBits;
    uint8_t offline = 0;
    uint8_t xiaozhi_auto_return = 0;
    uint8_t page_order[kWorkPageCount] = {};
    bool have_page_order = false;
};

static_assert(array_count(kSavedConfigKeys) > 0, "NVS config key registry must not be empty");
static_assert(cstr_array_nonempty(kSavedConfigKeys),
              "NVS config keys must be non-empty");
static_assert(cstr_array_unique(kSavedConfigKeys), "factory reset config keys must be unique");
static_assert(cstr_array_contains(kSavedConfigKeys, kWifiSsidKey), "factory reset must clear Wi-Fi SSID");
static_assert(cstr_array_contains(kSavedConfigKeys, kWifiPassKey), "factory reset must clear Wi-Fi password");
static_assert(cstr_array_contains(kSavedConfigKeys, kWeatherApiKeyKey), "factory reset must clear weather API key");
static_assert(cstr_array_contains(kSavedConfigKeys, kManualWeatherCityKey),
              "factory reset must clear manual weather city");
static_assert(cstr_array_contains(kSavedConfigKeys, kIgnoredAssetWeatherCityKey),
              "factory reset must clear ignored asset weather city");
static_assert(cstr_array_contains(kSavedConfigKeys, kLegacyApiHostKey), "factory reset must clear legacy API host");
static_assert(cstr_array_contains(kSavedConfigKeys, kOfflineModeKey), "factory reset must clear offline mode");
static_assert(cstr_array_contains(kSavedConfigKeys, kHourlyChimeKey),
              "factory reset must clear hourly reminder");
static_assert(cstr_array_contains(kSavedConfigKeys, kHourlyAllDayKey),
              "factory reset must clear all-day reminder");
static_assert(cstr_array_contains(kSavedConfigKeys, kChimeVolumeKey),
              "factory reset must clear reminder volume");
static_assert(cstr_array_contains(kSavedConfigKeys, kChimeSoundKey),
              "factory reset must clear reminder sound");
static_assert(cstr_array_contains(kSavedConfigKeys, kPageMaskV5Key),
              "factory reset must clear current page mask");
static_assert(cstr_array_contains(kSavedConfigKeys, kPageOrderV5Key),
              "factory reset must clear current page order");
static_assert(cstr_array_contains(kSavedConfigKeys, kXiaozhiAutoReturnKey),
              "factory reset must clear Xiaozhi auto-return setting");
static_assert(array_count(kConfigEventReasonTexts) > 0, "config event reason registry must not be empty");
static_assert(array_count(kNvsActionTexts) > 0, "NVS action text registry must not be empty");
static_assert(array_count(kConfigWarningTexts) > 0, "configuration warning text registry must not be empty");
static_assert(cstr_array_nonempty(kConfigEventReasonTexts),
              "config event caller reasons must be non-empty");
static_assert(cstr_array_nonempty(kNvsActionTexts), "NVS action texts must be non-empty");
static_assert(cstr_array_nonempty(kConfigWarningTexts),
              "configuration warning texts must be non-empty");
static_assert(kWorkPageCount <= 8, "work page enabled mask is stored as uint8_t");
static_assert((kPageMaskV4KnownBits & work_page_mask_bit(kWorkPageXiaozhiAI)) == 0,
              "page mask v4 must not include Xiaozhi AI page");
static_assert(kPageMaskV5KnownBits == all_work_page_mask(),
              "page mask v5 must cover every current work page");
static_assert((kDefaultWorkPageMask & kPageMaskV5KnownBits) == kDefaultWorkPageMask &&
                  kDefaultWorkPageMask != 0,
              "default work page mask must enable at least one known page");
static_assert((kPageMaskV5KnownBits & kWeatherBoardPageMask) == kWeatherBoardPageMask,
              "weather board page must be covered by the current page mask");
static_assert((kPageMaskV5KnownBits & kFlipClockPageMask) == kFlipClockPageMask,
              "flip clock page must be covered by the current page mask");

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

esp_err_t write_manual_weather_city_key(nvs_handle_t nvs, const char *city)
{
    return write_optional_nvs_string_key(nvs, kManualWeatherCityKey, city);
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
    if (!app_text::output_buffer_available(out, out_len)) {
        return false;
    }
    out[0] = '\0';
    if (!custom_assets_read_weather_city(out, out_len)) {
        return false;
    }
    char normalized[kManualWeatherCityLen] = {};
    if (!normalize_weather_city_input(out, normalized, sizeof(normalized)) ||
        normalized[0] == '\0' || strlen(normalized) >= out_len) {
        return false;
    }
    strlcpy(out, normalized, out_len);
    return true;
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

uint8_t read_saved_page_mask(nvs_handle_t nvs)
{
    uint8_t page_mask = kDefaultWorkPageMask;
    if (nvs_get_u8(nvs, kPageMaskV5Key, &page_mask) == ESP_OK) {
        return normalize_work_page_enabled_mask(page_mask);
    }
    if (nvs_get_u8(nvs, kPageMaskV4Key, &page_mask) == ESP_OK) {
        return normalize_work_page_enabled_mask(
            static_cast<uint8_t>(page_mask | work_page_mask_bit(kWorkPageXiaozhiAI)));
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
    for (const char *key : kSavedConfigKeys) {
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
    ScopedNvsHandle nvs;
    esp_err_t open_err = nvs.open(NVS_READWRITE, kNvsActionClearingConfig);
    if (open_err != ESP_OK) {
        return false;
    }
    esp_err_t err = erase_saved_config_keys(nvs.get());
    nvs.close();
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
        char normalized[kManualWeatherCityLen] = {};
        if (!normalize_weather_city_input(g_manual_weather_city,
                                          normalized,
                                          sizeof(normalized))) {
            ESP_LOGW(TAG, "%s", kInvalidWeatherCityLoadLog);
            g_manual_weather_city[0] = '\0';
        } else {
            strlcpy(g_manual_weather_city, normalized, sizeof(g_manual_weather_city));
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

bool apply_loaded_page_config(uint8_t page_mask, const uint8_t *page_order, bool have_page_order)
{
    g_work_page_enabled_mask = normalize_work_page_enabled_mask(page_mask);
    if (have_page_order && page_order) {
        memcpy(g_work_page_order, page_order, sizeof(g_work_page_order));
    }
    normalize_work_page_order();
    uint8_t online_mask = g_work_page_enabled_mask;
    if (g_offline_mode_ui_enabled) {
        g_work_page_enabled_mask = work_page_mask_for_offline_mode(g_work_page_enabled_mask);
    }
    g_active_work_page = first_enabled_work_page();
    return online_mask != g_work_page_enabled_mask;
}

LoadedNetworkConfig read_loaded_network_config(nvs_handle_t nvs)
{
    LoadedNetworkConfig loaded = {};
    loaded.ssid_err = read_nvs_string(nvs, kWifiSsidKey, g_wifi_ssid, sizeof(g_wifi_ssid));
    loaded.pass_err = read_nvs_string(nvs, kWifiPassKey, g_wifi_pass, sizeof(g_wifi_pass));
    loaded.key_err = read_nvs_string(nvs, kWeatherApiKeyKey, g_weather_api_key, sizeof(g_weather_api_key));
    loaded.chime = read_nvs_u8_or_default(nvs, kHourlyChimeKey, 0);
    loaded.all_day = read_nvs_u8_or_default(nvs, kHourlyAllDayKey, 0);
    loaded.volume = read_nvs_u8_or_default(nvs,
                                           kChimeVolumeKey,
                                           chime_settings::kDefaultVolumePercent);
    loaded.sound = read_nvs_u8_or_default(nvs, kChimeSoundKey, 0);
    loaded.page_mask = read_saved_page_mask(nvs);
    loaded.offline = read_nvs_u8_or_default(nvs, kOfflineModeKey, 0);
    loaded.xiaozhi_auto_return = read_nvs_u8_or_default(nvs, kXiaozhiAutoReturnKey, 0);
    load_saved_manual_weather_city(nvs);
    loaded.have_page_order = read_saved_page_order(nvs, loaded.page_order, sizeof(loaded.page_order));
    return loaded;
}

bool apply_loaded_network_config(const LoadedNetworkConfig &loaded)
{
    g_have_weather_key = loaded.key_err == ESP_OK && g_weather_api_key[0] != '\0';
    g_has_manual_weather_city = g_manual_weather_city[0] != '\0';
    g_hourly_chime_enabled = nvs_u8_to_bool(loaded.chime);
    g_hourly_chime_all_day = nvs_u8_to_bool(loaded.all_day);
    g_offline_mode_ui_enabled = nvs_u8_to_bool(loaded.offline);
    g_xiaozhi_auto_return_enabled = nvs_u8_to_bool(loaded.xiaozhi_auto_return);
    g_chime_volume_percent = chime_settings::normalize_stored_volume(loaded.volume);
    g_chime_sound_index = normalize_chime_sound_index(loaded.sound);
    return apply_loaded_page_config(loaded.page_mask, loaded.page_order, loaded.have_page_order);
}
} // namespace

bool load_saved_config()
{
    ScopedNvsHandle nvs;
    esp_err_t open_err = nvs.open(NVS_READONLY, kNvsActionLoadingConfig, false);
    if (open_err != ESP_OK) {
        return false;
    }
    LoadedNetworkConfig loaded = read_loaded_network_config(nvs.get());
    nvs.close();
    bool offline_page_mask_changed = apply_loaded_network_config(loaded);
    if (offline_page_mask_changed && !save_work_page_settings()) {
        ESP_LOGW(TAG, "%s", kOfflinePageMaskPersistFailedLog);
    }
    return loaded.ssid_err == ESP_OK && loaded.pass_err == ESP_OK && g_wifi_ssid[0] != '\0';
}

void clear_network_request_bits()
{
    clear_config_event_bits(kNetworkRequestClearBits, kConfigEventReasonNetworkRequestReset);
}

bool set_offline_mode_enabled(bool enabled)
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingOfflineMode);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t next_value = bool_to_nvs_u8(enabled);
    uint8_t next_page_mask = enabled
                                 ? work_page_mask_for_offline_mode(g_work_page_enabled_mask)
                                 : g_work_page_enabled_mask;
    bool offline_changed = false;
    bool page_mask_changed = false;
    err = write_changed_nvs_u8(nvs.get(), err, kOfflineModeKey, next_value, &offline_changed);
    if (enabled) {
        err = write_changed_nvs_u8(nvs.get(), err, kPageMaskV5Key, next_page_mask, &page_mask_changed);
    }
    err = commit_nvs_if_changed(nvs.get(), err, offline_changed || page_mask_changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_OFFLINE_MODE_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    g_offline_mode_ui_enabled = enabled;
    if (enabled) {
        g_work_page_enabled_mask = next_page_mask;
        normalize_work_page_order();
        ensure_active_work_page_enabled();
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

bool is_weather_city_input_valid(const char *city)
{
    return weather_city_text::input_valid(city, kManualWeatherCityLen);
}

bool normalize_weather_city_input(const char *city, char *out, size_t out_len)
{
    return weather_city_text::normalize(city, out, out_len);
}

void copy_trimmed_weather_city(char *out, size_t out_len, const char *city)
{
    if (!normalize_weather_city_input(city, out, out_len) && app_text::output_buffer_available(out, out_len)) {
        out[0] = '\0';
    }
}

void set_manual_weather_city_state(const char *city)
{
    strlcpy(g_manual_weather_city, cstr_or_empty(city), sizeof(g_manual_weather_city));
    g_has_manual_weather_city = g_manual_weather_city[0] != '\0';
}

bool finish_manual_weather_city_save(ScopedNvsHandle &nvs,
                                     esp_err_t err,
                                     const char *city,
                                     bool changed)
{
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    nvs.close();
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
    g_xiaozhi_auto_return_enabled = false;
    g_hourly_chime_enabled = false;
    g_hourly_chime_all_day = false;
    g_chime_volume_percent = chime_settings::kDefaultVolumePercent;
    g_chime_sound_index = 0;
    g_work_page_enabled_mask = kDefaultWorkPageMask;
    reset_work_page_order();
    g_active_work_page = first_enabled_work_page();
    clear_config_event_bits(kWifiConnectedBit | kWeatherReadyBit, kConfigEventReasonFactoryReset);
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
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingConfig);
    if (err != ESP_OK) {
        return false;
    }
    err = write_saved_config_nvs(nvs.get(), ssid, pass, api_key, city);
    err = commit_nvs_if_ok(nvs.get(), err);
    nvs.close();
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
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingWeatherCity);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_manual_weather_city_save_nvs(nvs.get(), next, &changed);
    return finish_manual_weather_city_save(nvs, err, next, changed);
}

bool clear_manual_weather_city()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionClearingWeatherCity);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = clear_manual_weather_city_nvs(nvs.get(), &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    nvs.close();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, NVS_CLEAR_WEATHER_CITY_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    set_manual_weather_city_state("");
    return true;
}

bool save_hourly_chime_setting()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingHourlyReminder);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t next_chime = bool_to_nvs_u8(g_hourly_chime_enabled);
    uint8_t next_all_day = bool_to_nvs_u8(g_hourly_chime_all_day);
    uint8_t next_volume = (uint8_t)g_chime_volume_percent;
    uint8_t next_sound = (uint8_t)g_chime_sound_index;
    bool changed = false;
    err = write_hourly_chime_settings_nvs(nvs.get(),
                                          err,
                                          next_chime,
                                          next_all_day,
                                          next_volume,
                                          next_sound,
                                          &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_work_page_settings()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingPageSettings);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t mask = normalize_work_page_enabled_mask(g_work_page_enabled_mask);
    bool changed = false;
    err = write_changed_nvs_u8(nvs.get(), err, kPageMaskV5Key, mask, &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    g_work_page_enabled_mask = mask;
    return true;
}

bool save_work_page_order()
{
    normalize_work_page_order();
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingPageOrder);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_work_page_order_nvs(nvs.get(),
                                    err,
                                    g_work_page_order,
                                    sizeof(g_work_page_order),
                                    &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_ORDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_xiaozhi_auto_return_setting()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingXiaozhiAutoReturn);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_changed_nvs_u8(nvs.get(),
                               err,
                               kXiaozhiAutoReturnKey,
                               bool_to_nvs_u8(g_xiaozhi_auto_return_enabled),
                               &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_XIAOZHI_AUTO_RETURN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool clear_saved_config()
{
    if (!clear_saved_config_nvs()) {
        return false;
    }
    if (!alarm_clear_saved_state()) {
        return false;
    }
    reset_saved_config_runtime_state();
    return true;
}
