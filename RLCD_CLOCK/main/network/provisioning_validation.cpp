// 在網路任務上驗證 Open-Meteo 公開端點與可選手動城市。
#include "provisioning_validation.h"

#include "app_metadata.h"
#include "manual_weather_city_state.h"
#include "open_meteo_client.h"

#include "esp_attr.h"
#include "esp_log.h"

namespace {
constexpr size_t kProvisioningWeatherCityNameSize = 32;
#define PROVISIONING_MANUAL_WEATHER_CITY_VALIDATED_FORMAT \
    "provisioning manual weather city validated: %s id=%s"
constexpr const char *kProvisioningWeatherApiValidationFailedLog =
    "provisioning weather API validation failed";
constexpr const char *kProvisioningWeatherCityValidationFailedLog =
    "provisioning manual weather city validation failed";
constexpr const char *kProvisioningWeatherCityValidationErrorLog =
    "provisioning manual weather city validation interrupted";

// Validation runs only in the serialized network task. Keep the probe result
// out of that task's stack while HTTPS owns its TLS memory.
EXT_RAM_BSS_ATTR WeatherData s_provisioning_probe_weather;
static_assert(sizeof(WeatherData) >= 96,
              "provisioning weather probe should remain off the network task stack");
} // namespace

WifiPortalSaveResult validate_saved_provisioning_weather_configuration()
{
    char weather_city[kManualWeatherCityLen] = {};
    if (!manual_weather_city_snapshot(weather_city, sizeof(weather_city))) {
        // Verify the public endpoint without a user city.
        WeatherForecastData forecast = {};
        if (open_meteo_fetch_weather("39.90", "116.40", "北京",
                                     &s_provisioning_probe_weather, &forecast) !=
            OpenMeteoResult::kOk) {
            ESP_LOGW(TAG, "%s", kProvisioningWeatherApiValidationFailedLog);
            return WifiPortalSaveResult::kWeatherApiFailed;
        }
        return WifiPortalSaveResult::kSuccess;
    }
    char city_name[kProvisioningWeatherCityNameSize] = {};
    char latitude[16] = {};
    char longitude[16] = {};
    OpenMeteoResult status = open_meteo_lookup_city(weather_city,
                                                              city_name, sizeof(city_name),
                                                              latitude, sizeof(latitude),
                                                              longitude, sizeof(longitude));
    if (status == OpenMeteoResult::kOk) {
        ESP_LOGI(TAG,
                 PROVISIONING_MANUAL_WEATHER_CITY_VALIDATED_FORMAT,
                 city_name,
                 "coordinates");
        return WifiPortalSaveResult::kSuccess;
    }
    if (status == OpenMeteoResult::kNotFound) {
        ESP_LOGW(TAG, "%s", kProvisioningWeatherCityValidationFailedLog);
        return WifiPortalSaveResult::kWeatherCityInvalid;
    }
    ESP_LOGW(TAG, "%s", kProvisioningWeatherCityValidationErrorLog);
    return WifiPortalSaveResult::kWeatherApiFailed;
}
