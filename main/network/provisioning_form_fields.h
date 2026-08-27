// Declares the Wi-Fi and NTP fields accepted by Settings Mode.
#pragma once

#include "ntp_server_config.h"

#include <stddef.h>

inline constexpr size_t kProvisioningSsidFieldSize = 33;
inline constexpr size_t kProvisioningPasswordFieldSize = 65;
inline constexpr size_t kProvisioningNtpServerFieldSize = kNtpServerNameLen;

struct ProvisioningFormFields {
    char ssid[kProvisioningSsidFieldSize] = {};
    char pass[kProvisioningPasswordFieldSize] = {};
    char backup_ssid[kProvisioningSsidFieldSize] = {};
    char backup_pass[kProvisioningPasswordFieldSize] = {};
    char ntp_server[kProvisioningNtpServerFieldSize] = {};
};

void read_provisioning_form_fields(const char *body, ProvisioningFormFields *fields);
