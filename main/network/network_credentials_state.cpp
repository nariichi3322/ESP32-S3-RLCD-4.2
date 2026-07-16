// 集中维护 Wi-Fi 凭据、天气 API Key 及其可用状态的完整快照。
#include "network_credentials_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <stdint.h>
#include <string.h>

namespace {
constexpr uint8_t kWifiConfiguredMask = 1U << 0;
constexpr uint8_t kWeatherApiKeyConfiguredMask = 1U << 1;
StaticSemaphore_t s_credentials_mutex_storage = {};
SemaphoreHandle_t s_credentials_mutex = nullptr;
NetworkCredentialsSnapshot s_credentials;
std::atomic<uint8_t> s_credentials_availability_mask{0};

static_assert((kWifiConfiguredMask & kWeatherApiKeyConfiguredMask) == 0,
              "credential availability flags must not overlap");

bool lock_credentials()
{
    return s_credentials_mutex &&
           xSemaphoreTake(s_credentials_mutex, portMAX_DELAY) == pdTRUE;
}

void unlock_credentials()
{
    xSemaphoreGive(s_credentials_mutex);
}

uint8_t credentials_availability_mask(const NetworkCredentialsSnapshot &credentials)
{
    uint8_t mask = 0;
    if (credentials.wifi_configured) {
        mask |= kWifiConfiguredMask;
    }
    if (credentials.weather_api_key_configured) {
        mask |= kWeatherApiKeyConfiguredMask;
    }
    return mask;
}

template <size_t N>
void copy_text(char (&out)[N], const char *value)
{
    const char *source = value ? value : "";
    const size_t length = strnlen(source, N - 1);
    memcpy(out, source, length);
    out[length] = '\0';
}

template <size_t N>
bool copy_field_snapshot(char *out, size_t out_len, const char (&field)[N], const bool &configured)
{
    if (!out || out_len < N) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }
    if (!lock_credentials()) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, field, N);
    const bool available = configured && out[0] != '\0';
    unlock_credentials();
    return available;
}
} // namespace

bool network_credentials_state_init()
{
    if (s_credentials_mutex) {
        return true;
    }
    s_credentials_mutex =
        xSemaphoreCreateMutexStatic(&s_credentials_mutex_storage);
    return s_credentials_mutex != nullptr;
}

void network_credentials_snapshot(NetworkCredentialsSnapshot *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!lock_credentials()) {
        return;
    }
    memcpy(out, &s_credentials, sizeof(*out));
    unlock_credentials();
}

NetworkCredentialsAvailability network_credentials_availability()
{
    const uint8_t mask =
        s_credentials_availability_mask.load(std::memory_order_acquire);
    const NetworkCredentialsAvailability availability = {
        (mask & kWifiConfiguredMask) != 0,
        (mask & kWeatherApiKeyConfiguredMask) != 0,
    };
    return availability;
}

void network_credentials_store(const char *ssid,
                               const char *password,
                               const char *weather_api_key,
                               bool wifi_configured,
                               bool weather_api_key_configured)
{
    NetworkCredentialsSnapshot replacement;
    copy_text(replacement.wifi_ssid, ssid);
    copy_text(replacement.wifi_password, password);
    copy_text(replacement.weather_api_key, weather_api_key);
    replacement.wifi_configured = wifi_configured && replacement.wifi_ssid[0] != '\0';
    replacement.weather_api_key_configured =
        weather_api_key_configured && replacement.weather_api_key[0] != '\0';

    if (!lock_credentials()) {
        return;
    }
    memcpy(&s_credentials, &replacement, sizeof(s_credentials));
    s_credentials_availability_mask.store(
        credentials_availability_mask(replacement),
        std::memory_order_release);
    unlock_credentials();
}

void network_credentials_clear()
{
    network_credentials_store("", "", "", false, false);
}

bool network_wifi_credentials_configured()
{
    return network_credentials_availability().wifi_configured;
}

bool network_weather_api_key_configured()
{
    return network_credentials_availability().weather_api_key_configured;
}

bool network_all_online_credentials_configured()
{
    const NetworkCredentialsAvailability availability = network_credentials_availability();
    return availability.wifi_configured && availability.weather_api_key_configured;
}

bool network_wifi_ssid_snapshot(char *out, size_t out_len)
{
    return copy_field_snapshot(
        out, out_len, s_credentials.wifi_ssid, s_credentials.wifi_configured);
}

bool network_weather_api_key_snapshot(char *out, size_t out_len)
{
    return copy_field_snapshot(
        out, out_len, s_credentials.weather_api_key, s_credentials.weather_api_key_configured);
}
