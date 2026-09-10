// Declares the Wi-Fi and NTP fields accepted by Settings Mode.
#pragma once

#include "ntp_server_config.h"
#include "weather_city_contract.h"

#include <stddef.h>

inline constexpr size_t kProvisioningSsidFieldSize = 33;
inline constexpr size_t kProvisioningPasswordFieldSize = 65;
inline constexpr size_t kProvisioningNtpServerFieldSize = kNtpServerNameLen;
inline constexpr size_t kProvisioningManualTimeFieldSize = 24;
inline constexpr size_t kProvisioningWeatherCityFieldSize = kManualWeatherCityLen;

struct ProvisioningFormFields {
    char ssid[kProvisioningSsidFieldSize] = {};
    char pass[kProvisioningPasswordFieldSize] = {};
    char backup_ssid[kProvisioningSsidFieldSize] = {};
    char backup_pass[kProvisioningPasswordFieldSize] = {};
    char ntp_server[kProvisioningNtpServerFieldSize] = {};
    char weather_city[kProvisioningWeatherCityFieldSize] = {};
};

void read_provisioning_form_fields(const char *body, ProvisioningFormFields *fields);
void read_provisioning_manual_time(const char *body, char *out, size_t out_len);
