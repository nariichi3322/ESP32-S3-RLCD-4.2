// 在启动阶段读取 NVS，并一次发布联网凭据和普通设备设置运行态。
#include "saved_config_loader.h"

#include "active_work_page_state.h"
#include "app_metadata.h"
#include "chime_runtime_state.h"
#include "chime_settings.h"
#include "device_settings_persistence.h"
#include "manual_weather_city_state.h"
#include "network_chime_storage.h"
#include "network_config_keys.h"
#include "network_config_nvs.h"
#include "network_credentials_state.h"
#include "network_page_storage.h"
#include "network_page_storage_policy.h"
#include "network_weather_city_storage.h"
#include "offline_mode_state.h"
#include "ui_work_page_catalog.h"
#include "xiaozhi_auto_return_state.h"

#include <esp_log.h>

using network_config_nvs::read_nvs_string;
using network_config_nvs::read_nvs_u8_or_default;
using network_config_nvs::ScopedNvsHandle;
using network_page_storage::read_saved_page_mask;
using network_page_storage::read_saved_page_order;
using network_config_keys::kOfflineModeKey;
using network_config_keys::kWeatherApiKeyKey;
using network_config_keys::kWifiPassKey;
using network_config_keys::kWifiSsidKey;
using network_config_keys::kXiaozhiAutoReturnKey;

namespace {
constexpr uint8_t work_page_mask_bit(int page)
{
    return static_cast<uint8_t>(1U << page);
}

constexpr uint8_t kPageMaskV4KnownBits = network_page_storage::kLegacyV4KnownPageMask;
constexpr uint8_t kPageMaskV5KnownBits = network_page_storage::kCurrentKnownPageMask;
constexpr uint8_t kWeatherBoardPageMask = work_page_mask_bit(kWorkPageWeatherBoard);
constexpr uint8_t kFlipClockPageMask = work_page_mask_bit(kWorkPageFlipClock);
constexpr const char *kNvsActionLoadingConfig = "loading config";
constexpr const char *kOfflinePageMaskPersistFailedLog =
    "failed to persist offline-compatible page settings";

struct LoadedSavedConfig {
    esp_err_t ssid_err = ESP_FAIL;
    esp_err_t pass_err = ESP_FAIL;
    esp_err_t key_err = ESP_FAIL;
    char wifi_ssid[kNetworkWifiSsidLen] = {};
    char wifi_password[kNetworkWifiPasswordLen] = {};
    char weather_api_key[kNetworkWeatherApiKeyLen] = {};
    uint8_t chime = 0;
    uint8_t all_day = 0;
    uint8_t volume = chime_settings::kDefaultVolumePercent;
    uint8_t sound = 0;
    uint8_t page_mask = kPageMaskV5KnownBits;
    uint8_t offline = 0;
    uint8_t xiaozhi_auto_return = 0;
    uint8_t page_order[kWorkPageCount] = {};
    char manual_weather_city[kManualWeatherCityLen] = {};
    bool have_page_order = false;
};

static_assert(kWorkPageCount <= 8, "work page enabled mask is stored as uint8_t");
static_assert((kPageMaskV4KnownBits & work_page_mask_bit(kWorkPageXiaozhiAI)) == 0,
              "page mask v4 must not include Xiaozhi AI page");
static_assert(kPageMaskV5KnownBits == static_cast<uint8_t>((1U << kWorkPageCount) - 1U),
              "page mask v5 must cover every current work page");
static_assert((kPageMaskV5KnownBits & kWeatherBoardPageMask) == kWeatherBoardPageMask,
              "weather board page must be covered by the current page mask");
static_assert((kPageMaskV5KnownBits & kFlipClockPageMask) == kFlipClockPageMask,
              "flip clock page must be covered by the current page mask");

uint8_t normalize_chime_sound_index(uint8_t sound)
{
    return sound < chime_settings::kSoundCount ? sound : 0;
}

constexpr bool nvs_u8_to_bool(uint8_t value)
{
    return value != 0;
}

bool apply_loaded_page_config(uint8_t page_mask,
                              const uint8_t *page_order,
                              bool have_page_order)
{
    work_page_enabled_mask_store(normalize_work_page_enabled_mask(page_mask));
    if (have_page_order && page_order) {
        work_page_order_replace(page_order, kWorkPageCount);
    } else {
        normalize_work_page_order();
    }
    uint8_t online_mask = work_page_enabled_mask_load();
    if (offline_mode_enabled_load()) {
        work_page_enabled_mask_store(work_page_mask_for_offline_mode(online_mask));
    }
    active_work_page_store(first_enabled_work_page());
    return online_mask != work_page_enabled_mask_load();
}

LoadedSavedConfig read_saved_config(nvs_handle_t nvs)
{
    LoadedSavedConfig loaded = {};
    loaded.ssid_err =
        read_nvs_string(nvs, kWifiSsidKey, loaded.wifi_ssid, sizeof(loaded.wifi_ssid));
    loaded.pass_err = read_nvs_string(
        nvs, kWifiPassKey, loaded.wifi_password, sizeof(loaded.wifi_password));
    loaded.key_err = read_nvs_string(
        nvs, kWeatherApiKeyKey, loaded.weather_api_key, sizeof(loaded.weather_api_key));
    network_chime_storage::StoredChimeSettings chime =
        network_chime_storage::read(nvs, chime_settings::kDefaultVolumePercent);
    loaded.chime = chime.enabled;
    loaded.all_day = chime.all_day;
    loaded.volume = chime.volume;
    loaded.sound = chime.sound;
    loaded.page_mask = read_saved_page_mask(nvs);
    loaded.offline = read_nvs_u8_or_default(nvs, kOfflineModeKey, 0);
    loaded.xiaozhi_auto_return = read_nvs_u8_or_default(nvs, kXiaozhiAutoReturnKey, 0);
    network_weather_city_storage::load_preferred_city(
        nvs, loaded.manual_weather_city, sizeof(loaded.manual_weather_city));
    loaded.have_page_order =
        read_saved_page_order(nvs, loaded.page_order, sizeof(loaded.page_order));
    return loaded;
}

bool apply_loaded_config(const LoadedSavedConfig &loaded)
{
    const bool wifi_configured = loaded.ssid_err == ESP_OK &&
                                 loaded.pass_err == ESP_OK &&
                                 loaded.wifi_ssid[0] != '\0';
    const bool weather_key_configured = loaded.key_err == ESP_OK &&
                                        loaded.weather_api_key[0] != '\0';
    network_credentials_store(loaded.wifi_ssid,
                              loaded.wifi_password,
                              loaded.weather_api_key,
                              wifi_configured,
                              weather_key_configured);
    manual_weather_city_store(loaded.manual_weather_city);
    ChimeRuntimeSnapshot chime = {
        nvs_u8_to_bool(loaded.chime),
        nvs_u8_to_bool(loaded.all_day),
        chime_settings::normalize_stored_volume(loaded.volume),
        static_cast<uint8_t>(normalize_chime_sound_index(loaded.sound)),
    };
    chime_runtime_snapshot_store(chime);
    offline_mode_enabled_store(nvs_u8_to_bool(loaded.offline));
    xiaozhi_auto_return_enabled_store(nvs_u8_to_bool(loaded.xiaozhi_auto_return));
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
    LoadedSavedConfig loaded = read_saved_config(nvs.get());
    nvs.close();
    bool offline_page_mask_changed = apply_loaded_config(loaded);
    if (offline_page_mask_changed && !save_work_page_settings()) {
        ESP_LOGW(TAG, "%s", kOfflinePageMaskPersistFailedLog);
    }
    return network_wifi_credentials_configured();
}
