// 处理配网页联网凭据和离线日期时间提交，不拥有 NVS key 细节。
#include "network_provisioning.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "network_config.h"
#include "network_config_internal.h"
#include "network_credentials_state.h"
#include "network_credentials_state_internal.h"
#include "ntp_server_config.h"
#include "provisioning_form_fields.h"
#include "wifi_portal_state_internal.h"

#include <esp_attr.h>
#include <esp_log.h>

#include <stdint.h>
#include <string.h>

namespace {
constexpr const char *kConfigEventReasonProvisioningSave = "provisioning save";
#define PROVISIONING_EMPTY_BODY_LOG "provisioning ignored empty request body"
#define PROVISIONING_EMPTY_SSID_LOG "provisioning ignored empty ssid"
#define PROVISIONING_INVALID_NTP_SERVER_LOG "provisioning ignored invalid NTP server"
#define PROVISIONING_DUPLICATE_WIFI_LOG \
    "provisioning ignored duplicate main and backup Wi-Fi SSID"
#define PROVISIONING_SAVED_FORMAT \
    "provisioning saved main_ssid=%s main_pass_len=%u backup_ssid=%s backup_pass_len=%u ntp_server=%s"

// Synchronous setup handlers share one HTTP server task, so only one save
// request can own this workspace at a time.
EXT_RAM_BSS_ATTR ProvisioningFormFields s_provisioning_form_fields_workspace;
static_assert(sizeof(s_provisioning_form_fields_workspace) >=
                  kProvisioningSsidFieldSize +
                      kProvisioningPasswordFieldSize +
                      kProvisioningSsidFieldSize +
                      kProvisioningPasswordFieldSize +
                      kProvisioningNtpServerFieldSize,
              "provisioning form workspace must contain every bounded field");

void clear_provisioning_form_fields_workspace()
{
    volatile uint8_t *bytes =
        reinterpret_cast<volatile uint8_t *>(
            &s_provisioning_form_fields_workspace);
    for (size_t remaining = sizeof(s_provisioning_form_fields_workspace);
         remaining > 0;
         --remaining) {
        *bytes++ = 0;
    }
}

class ProvisioningFormFieldsWorkspaceGuard {
public:
    ProvisioningFormFieldsWorkspaceGuard()
    {
        clear_provisioning_form_fields_workspace();
    }

    ~ProvisioningFormFieldsWorkspaceGuard()
    {
        clear_provisioning_form_fields_workspace();
    }

    ProvisioningFormFieldsWorkspaceGuard(
        const ProvisioningFormFieldsWorkspaceGuard &) = delete;
    ProvisioningFormFieldsWorkspaceGuard &operator=(
        const ProvisioningFormFieldsWorkspaceGuard &) = delete;

    ProvisioningFormFields &fields()
    {
        return s_provisioning_form_fields_workspace;
    }
};

void preserve_saved_wifi_password(WifiCredentialSlot slot,
                                  const char *submitted_ssid,
                                  char *submitted_password,
                                  size_t submitted_password_len)
{
    if (!submitted_ssid || submitted_ssid[0] == '\0' ||
        !submitted_password || submitted_password_len == 0 ||
        submitted_password[0] != '\0') {
        return;
    }
    char saved_ssid[kNetworkWifiSsidLen] = {};
    char saved_password[kNetworkWifiPasswordLen] = {};
    if (network_wifi_credentials_for_slot_copy(slot,
                                               saved_ssid,
                                               sizeof(saved_ssid),
                                               saved_password,
                                               sizeof(saved_password)) &&
        strcmp(saved_ssid, submitted_ssid) == 0) {
        strlcpy(submitted_password,
                saved_password,
                submitted_password_len);
    }
}
} // namespace

bool save_credentials_from_body(const char *body)
{
    if (!body) {
        ESP_LOGW(TAG, "%s", PROVISIONING_EMPTY_BODY_LOG);
        return false;
    }
    ProvisioningFormFieldsWorkspaceGuard workspace;
    ProvisioningFormFields &fields = workspace.fields();
    read_provisioning_form_fields(body, &fields);
    if (fields.ssid[0] == '\0') {
        ESP_LOGW(TAG, "%s", PROVISIONING_EMPTY_SSID_LOG);
        return false;
    }
    const WifiCredentialSlot preferred_slot = network_wifi_preferred_slot();
    preserve_saved_wifi_password(preferred_slot,
                                 fields.ssid,
                                 fields.pass,
                                 sizeof(fields.pass));
    preserve_saved_wifi_password(
        wifi_alternate_credential_slot(preferred_slot),
        fields.backup_ssid,
        fields.backup_pass,
        sizeof(fields.backup_pass));
    if (fields.backup_ssid[0] != '\0' &&
        strcmp(fields.ssid, fields.backup_ssid) == 0) {
        ESP_LOGW(TAG, "%s", PROVISIONING_DUPLICATE_WIFI_LOG);
        return false;
    }
    if (fields.ntp_server[0] == '\0') {
        strlcpy(fields.ntp_server,
                kDefaultNtpServerName,
                sizeof(fields.ntp_server));
    }
    if (!normalize_ntp_server_name(fields.ntp_server,
                                   fields.ntp_server,
                                   sizeof(fields.ntp_server))) {
        ESP_LOGW(TAG, "%s", PROVISIONING_INVALID_NTP_SERVER_LOG);
        return false;
    }
    ESP_LOGI(TAG, PROVISIONING_SAVED_FORMAT,
             fields.ssid,
             (unsigned)strlen(fields.pass),
              fields.backup_ssid[0] ? fields.backup_ssid : "none",
              (unsigned)strlen(fields.backup_pass),
              fields.ntp_server);
    clear_wifi_last_disconnect_reason();
    clear_config_event_bits(kWifiConnectedBit, kConfigEventReasonProvisioningSave);
    if (!save_config(fields.ssid,
                     fields.pass,
                     fields.backup_ssid,
                     fields.backup_pass,
                     fields.ntp_server,
                     "",
                     "",
                     "")) {
        return false;
    }
    return true;
}
