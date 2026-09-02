// 聲明跨任務使用的 Wi-Fi 憑據窄複製介面。
#pragma once

#include "wifi_failover_policy.h"

#include <stddef.h>

inline constexpr size_t kNetworkWifiSsidLen = 33;
inline constexpr size_t kNetworkWifiPasswordLen = 65;
struct NetworkCredentialsAvailability {
    bool wifi_configured = false;
};

// ESP-IDF STA-sized outputs use all bytes for maximum-length fields and are
// therefore not NUL-terminated in that one valid boundary case.
bool network_wifi_credentials_copy(char *ssid,
                                   size_t ssid_len,
                                   char *password,
                                   size_t password_len);
NetworkCredentialsAvailability network_credentials_availability();
bool network_wifi_credentials_configured();
bool network_weather_configuration_configured();
bool network_all_online_credentials_configured();
bool network_wifi_ssid_snapshot(char *out, size_t out_len);
bool network_wifi_alternate_ssid_snapshot(char *out, size_t out_len);
