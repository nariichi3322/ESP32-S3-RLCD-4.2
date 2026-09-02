// 处理配网页联网凭据和离线日期时间提交，不拥有 NVS key 细节。
#include "network_provisioning.h"

#include "app_event_group.h"
#include "app_metadata.h"
#include "alarm_services.h"
#include "housekeeping_schedule_notify.h"
#include "manual_time_parser.h"
#include "network_config.h"
#include "network_config_internal.h"
#include "network_credentials_state.h"
#include "network_credentials_state_internal.h"
#include "ntp_server_config.h"
#include "provisioning_form_fields.h"
#include "rtc_services.h"
#include "wifi_portal_state_internal.h"

#include <esp_attr.h>
#include <esp_log.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

namespace {
constexpr const char *kConfigEventReasonProvisioningSave = "provisioning save";
constexpr const char *kConfigEventReasonOfflineManualTime = "offline manual time";
#define PROVISIONING_EMPTY_BODY_LOG "provisioning ignored empty request body"
#define PROVISIONING_EMPTY_SSID_LOG "provisioning ignored empty ssid"
#define PROVISIONING_INVALID_NTP_SERVER_LOG "provisioning ignored invalid NTP server"
#define PROVISIONING_INVALID_WEATHER_CITY_LOG "provisioning ignored invalid weather city"
#define OFFLINE_SETUP_INVALID_MANUAL_TIME_LOG "offline setup ignored invalid manual time"
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
                      kProvisioningNtpServerFieldSize +
                      kProvisioningWeatherCityFieldSize,
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

bool save_offline_datetime_from_body(const char *body)
{
    if (!body) return false;
    char manual_time[kProvisioningManualTimeFieldSize] = {};
    read_provisioning_manual_time(body, manual_time, sizeof(manual_time));
    struct tm local = {};
    if (!parse_manual_datetime_text(manual_time, &local)) {
        ESP_LOGW(TAG, "%s", OFFLINE_SETUP_INVALID_MANUAL_TIME_LOG);
        return false;
    }
    const time_t epoch = mktime(&local);
    if (epoch <= 0) return false;
    struct timeval now = {};
    now.tv_sec = epoch;
    if (settimeofday(&now, nullptr) != 0) {
        ESP_LOGW(TAG, "offline time update failed errno=%d", errno);
        return false;
    }
    sync_rtc_from_system_time();
    alarm_notify_time_changed();
    notify_housekeeping_schedule_changed();
    if (!set_offline_mode_enabled(true)) return false;
    set_config_event_bits(kTimeSyncedBit, kConfigEventReasonOfflineManualTime);
    return true;
}

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
    if (!is_weather_city_input_valid(fields.weather_city)) {
        ESP_LOGW(TAG, "%s", PROVISIONING_INVALID_WEATHER_CITY_LOG);
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
                     fields.weather_city)) {
        return false;
    }
    return true;
}
