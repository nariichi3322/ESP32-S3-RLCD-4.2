// 执行设置页里的网络诊断流程，逐项显示联网链路状态。
#include "network_diagnostics_internal.h"

#include "daily_saying_service.h"
#include "ip_geolocation_client.h"

#include "app_constexpr.h"
#include "app_event_group.h"
#include "app_metadata.h"
#include "app_network_config.h"
#include "app_text_format.h"
#include "battery_runtime_state.h"
#include "network_credentials_state.h"
#include "network_diagnostics_catalog.h"
#include "network_diagnostics_probe.h"
#include "network_diagnostics_state_internal.h"
#include "network_sync_request_generation.h"
#include "network_sync_requests.h"
#include "network_sync_runtime.h"
#include "ntp_services.h"
#include "ui_settings_activity_state.h"
#include "ui_i18n.h"
#include "ui_task_notify.h"
#include "ui_language.h"
#include "weather_update.h"
#include "wifi_portal_state.h"

#include "esp_log.h"
#include "freertos/task.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t kNetworkDiagPublicIpResponseBufferSize = 2048;
constexpr size_t kNetworkDiagWideProbeBufferSize = 1024;
constexpr size_t kNetworkDiagLocationTextSize = 32;
constexpr size_t kNetworkDiagCityTextSize = 32;
constexpr size_t kNetworkDiagPublicIpTextSize = 48;
constexpr int kNetworkDiagNtpMaxRetries = 5;
constexpr const char *kNetworkDiagPublicIpUrl = "https://uapis.cn/api/v1/network/myip";
constexpr const char *kNetworkDiagGithubDnsHost = "raw.githubusercontent.com";
constexpr const char *kNetworkDiagForecastDnsHost = "api.open-meteo.com";
constexpr const char *kNetworkDiagGeocodingDnsHost = "geocoding-api.open-meteo.com";
constexpr const char *kNetworkDiagAirDnsHost = "air-quality-api.open-meteo.com";
struct NetworkDiagText {
    UiTextId id;
};

const char *network_diag_text(const NetworkDiagText &text)
{
    return ui_text(text.id);
}

constexpr NetworkDiagText kNetworkDiagStatusWaiting = {UiTextId::NetworkDiagStatusWaiting};
constexpr NetworkDiagText kNetworkDiagStatusChecking = {UiTextId::NetworkDiagnostics};
constexpr NetworkDiagText kNetworkDiagStatusFailed = {UiTextId::NetworkDiagStatusTimeout};
constexpr NetworkDiagText kNetworkDiagStatusOk = {UiTextId::NetworkDiagStatusSuccess};
constexpr NetworkDiagText kNetworkDiagStatusLowBatterySkipped = {UiTextId::NetworkDiagStatusLowBatterySkipped};
constexpr NetworkDiagText kNetworkDiagLocalIpPlaceholder = {UiTextId::NetworkDiagLocalIpPlaceholder};
constexpr NetworkDiagText kNetworkDiagPublicIpPlaceholder = {UiTextId::NetworkDiagPublicIpPlaceholder};
constexpr NetworkDiagText kNetworkDiagIpLocationFallback = {UiTextId::NetworkDiagIpLocationFallback};
constexpr NetworkDiagText kNetworkDiagDnsUnchecked = {UiTextId::NetworkDiagDnsUnchecked};
constexpr NetworkDiagText kNetworkDiagWeatherUnchecked = {UiTextId::NetworkDiagWeatherUnchecked};
constexpr NetworkDiagText kNetworkDiagNtpUnchecked = {UiTextId::NetworkDiagNtpUnchecked};
constexpr NetworkDiagText kNetworkDiagSayingUnchecked = {UiTextId::NetworkDiagSayingUnchecked};
constexpr NetworkDiagText kNetworkDiagInternetUnchecked = {UiTextId::NetworkDiagInternetUnchecked};
constexpr NetworkDiagText kNetworkDiagOtaSourceUnchecked = {UiTextId::NetworkDiagOtaSourceUnchecked};
constexpr NetworkDiagText kNetworkDiagLocalIpFormat = {UiTextId::NetworkDiagLocalIpFormat};
constexpr NetworkDiagText kNetworkDiagPublicIpFormat = {UiTextId::NetworkDiagPublicIpFormat};
constexpr NetworkDiagText kNetworkDiagIpLocationFormat = {UiTextId::NetworkDiagIpLocationFormat};
constexpr NetworkDiagText kNetworkDiagIpLocationCityFormat = {UiTextId::NetworkDiagIpLocationCityFormat};
constexpr NetworkDiagText kNetworkDiagDnsFormat = {UiTextId::NetworkDiagDnsFormat};
constexpr NetworkDiagText kNetworkDiagWeatherFormat = {UiTextId::NetworkDiagWeatherFormat};
constexpr NetworkDiagText kNetworkDiagNtpFormat = {UiTextId::NetworkDiagNtpFormat};
constexpr NetworkDiagText kNetworkDiagSayingFormat = {UiTextId::NetworkDiagSayingFormat};
constexpr NetworkDiagText kNetworkDiagInternetFormat = {UiTextId::NetworkDiagInternetFormat};
constexpr NetworkDiagText kNetworkDiagOtaFormat = {UiTextId::NetworkDiagOtaFormat};
constexpr NetworkDiagText kNetworkDiagOfflineModeEnabled = {UiTextId::NetworkDiagOfflineModeEnabled};
constexpr NetworkDiagText kNetworkDiagIpLocationWifiNotConfigured = {UiTextId::NetworkDiagIpWifiNotConfigured};
constexpr NetworkDiagText kNetworkDiagIpLocationWifiStartFailed = {UiTextId::NetworkDiagIpWifiStartFailed};
constexpr NetworkDiagText kNetworkDiagIpLocationPowerLockUnavailable = {UiTextId::NetworkDiagIpPowerLockUnavailable};
constexpr NetworkDiagText kNetworkDiagIpLocationWifiConnectTimeout = {UiTextId::NetworkDiagIpWifiConnectTimeout};
constexpr size_t kNetworkDiagIpv4TextMinSize = sizeof("255.255.255.255");
#define NETWORK_DIAG_LINE_INDEX_INVALID_FORMAT "network diag line index invalid: %d"
#define NETWORK_DIAG_LINE_FORMAT_FAILED_FORMAT "network diag line format failed index=%d"
#define NETWORK_DIAG_LINE_TRUNCATED_FORMAT "network diag line truncated index=%d len=%d"
static constexpr const char *kNetworkDiagBeginStateFailedLog =
    "network diag initial state publication failed";
static constexpr const char *kNetworkDiagTerminalStateFailedLog =
    "network diag terminal state publication failed";
void network_diag_set_line(int index, const char *fmt, ...);
constexpr int kNetworkDiagLineIndices[] = {
    kNetworkDiagLocalIpLine,
    kNetworkDiagPublicIpLine,
    kNetworkDiagIpLocationLine,
    kNetworkDiagDnsLine,
    kNetworkDiagWeatherLine,
    kNetworkDiagNtpLine,
    kNetworkDiagSayingLine,
    kNetworkDiagInternetLine,
    kNetworkDiagOtaLine,
};

void network_diag_clear_line(int index)
{
    network_diag_line_store(index, "");
}

void finish_network_diag_snapshot(const char *const *lines)
{
    if (network_diag_page_requested()) {
        settings_activity_record(xTaskGetTickCount());
    }
    if (!network_diag_state_publish(lines,
                                    kNetworkDiagLineCount,
                                    kNetworkDiagDone)) {
        ESP_LOGW(TAG, "%s", kNetworkDiagTerminalStateFailedLog);
        network_diag_state_store(kNetworkDiagDone);
    }
    notify_ui_task();
}

constexpr bool network_diag_lines_in_range()
{
    for (int index : kNetworkDiagLineIndices) {
        if (!network_diag_line_index_valid(index)) {
            return false;
        }
    }
    return true;
}

static_assert(kNetworkDiagWideProbeBufferSize > 0,
              "network diag wide probe buffer must be nonzero");
static_assert(kNetworkDiagPublicIpResponseBufferSize >= kNetworkDiagWideProbeBufferSize,
              "public IP response buffer must cover wide probe responses");
static_assert(kNetworkDiagLocationTextSize > 1, "network diag location text buffer must fit text and NUL");
static_assert(kNetworkDiagCityTextSize > 1, "network diag city text buffer must fit text and NUL");
static_assert(kNetworkDiagPublicIpTextSize > 1, "network diag public IP text buffer must fit text and NUL");
static_assert(kNetworkDiagPublicIpTextSize >= kNetworkDiagIpv4TextMinSize,
              "network diag public IP text buffer must fit IPv4 text");
static_assert(kNetworkDiagNtpMaxRetries > 0, "network diag NTP retry count must be positive");
static_assert(array_count(kNetworkDiagLineIndices) == kNetworkDiagLineCount,
              "network diag line index table must cover every UI row");
static_assert(network_diag_lines_in_range(), "network diag line indices must fit line table");
static_assert(kNetworkDiagOtaLine == kNetworkDiagLineCount - 1,
              "network diag OTA line must remain the final diagnostic row");
static_assert(kNetworkDiagLocalIpLine < kNetworkDiagPublicIpLine &&
                  kNetworkDiagPublicIpLine < kNetworkDiagIpLocationLine &&
                  kNetworkDiagIpLocationLine < kNetworkDiagDnsLine &&
                  kNetworkDiagDnsLine < kNetworkDiagWeatherLine &&
                  kNetworkDiagWeatherLine < kNetworkDiagNtpLine &&
                  kNetworkDiagNtpLine < kNetworkDiagSayingLine &&
                  kNetworkDiagSayingLine < kNetworkDiagInternetLine &&
                  kNetworkDiagInternetLine < kNetworkDiagOtaLine,
              "network diag line order must match UI initialization and execution order");

struct NetworkDiagLineFormat {
    int index;
    const NetworkDiagText *format;
};

constexpr NetworkDiagLineFormat kNetworkDiagInitialLines[] = {
    {kNetworkDiagLocalIpLine, &kNetworkDiagLocalIpFormat},
    {kNetworkDiagPublicIpLine, &kNetworkDiagPublicIpFormat},
    {kNetworkDiagIpLocationLine, &kNetworkDiagIpLocationFormat},
    {kNetworkDiagDnsLine, &kNetworkDiagDnsFormat},
    {kNetworkDiagWeatherLine, &kNetworkDiagWeatherFormat},
    {kNetworkDiagNtpLine, &kNetworkDiagNtpFormat},
    {kNetworkDiagSayingLine, &kNetworkDiagSayingFormat},
    {kNetworkDiagInternetLine, &kNetworkDiagInternetFormat},
    {kNetworkDiagOtaLine, &kNetworkDiagOtaFormat},
};

static_assert(array_count(kNetworkDiagInitialLines) == kNetworkDiagLineCount,
              "network diag initial line table must cover every UI row");
static_assert(kNetworkDiagInitialLines[0].index == kNetworkDiagLocalIpLine,
              "network diag initial table must follow visible row order");
static_assert(kNetworkDiagInitialLines[array_count(kNetworkDiagInitialLines) - 1].index == kNetworkDiagOtaLine,
              "network diag initial table must end with OTA row");

bool stop_remaining_network_diagnostics_if_low_battery(
    NetworkDiagLineIndex first_pending_line)
{
    if (!battery_low_mode_load()) {
        return false;
    }
    for (const auto &line : kNetworkDiagInitialLines) {
        if (line.index >= first_pending_line) {
            network_diag_set_line(line.index,
                                  network_diag_text(*line.format),
                                  network_diag_text(kNetworkDiagStatusLowBatterySkipped));
        }
    }
    return true;
}

bool network_diagnostics_should_continue(
    NetworkDiagLineIndex first_pending_line,
    bool &completed,
    uint32_t request_generation)
{
    if (!network_sync_request_is_current(kNetworkDiagBit,
                                         request_generation)) {
        completed = false;
        return false;
    }
    if (stop_remaining_network_diagnostics_if_low_battery(
            first_pending_line)) {
        completed = true;
        return false;
    }
    if (!network_sync_continuation_allowed()) {
        completed = false;
        return false;
    }
    return true;
}

const char *diag_result_text(bool ok)
{
    return network_diag_text(ok ? kNetworkDiagStatusOk : kNetworkDiagStatusFailed);
}

bool network_diag_text_matches(const char *text, const NetworkDiagText &localized)
{
    return text && strcmp(ui_language_localize(text),
                          network_diag_text(localized)) == 0;
}

const char *network_diag_external_text(const char *text)
{
    if (network_diag_text_matches(text, kNetworkDiagOfflineModeEnabled)) {
        return network_diag_text(kNetworkDiagOfflineModeEnabled);
    }
    if (network_diag_text_matches(text, kNetworkDiagStatusLowBatterySkipped)) {
        return network_diag_text(kNetworkDiagStatusLowBatterySkipped);
    }
    if (network_diag_text_matches(text, kNetworkDiagIpLocationWifiNotConfigured)) {
        return network_diag_text(kNetworkDiagIpLocationWifiNotConfigured);
    }
    if (network_diag_text_matches(text, kNetworkDiagIpLocationWifiStartFailed)) {
        return network_diag_text(kNetworkDiagIpLocationWifiStartFailed);
    }
    if (network_diag_text_matches(text, kNetworkDiagIpLocationPowerLockUnavailable)) {
        return network_diag_text(kNetworkDiagIpLocationPowerLockUnavailable);
    }
    if (network_diag_text_matches(text, kNetworkDiagIpLocationWifiConnectTimeout)) {
        return network_diag_text(kNetworkDiagIpLocationWifiConnectTimeout);
    }
    return ui_language_localize(text);
}

} // namespace

void network_diag_reset()
{
    network_diag_state_clear(kNetworkDiagIdle);
}

void network_diag_begin()
{
    const char *line_formats[kNetworkDiagLineCount] = {};
    for (const auto &line : kNetworkDiagInitialLines) {
        line_formats[line.index] = network_diag_text(*line.format);
    }
    if (!network_diag_state_begin(line_formats,
                                  kNetworkDiagLineCount,
                                  network_diag_text(kNetworkDiagStatusWaiting))) {
        ESP_LOGW(TAG, "%s", kNetworkDiagBeginStateFailedLog);
    }
    notify_ui_task();
}

void network_diag_finish()
{
    if (network_diag_page_requested()) {
        settings_activity_record(xTaskGetTickCount());
    }
    network_diag_state_store(kNetworkDiagDone);
    notify_ui_task();
}

void network_diag_finish_with_status(const char *status_text)
{
    if (!status_text) {
        network_diag_finish();
        return;
    }
    status_text = network_diag_external_text(status_text);
    const char *lines[kNetworkDiagLineCount] = {};
    for (auto &line : lines) {
        line = status_text;
    }
    finish_network_diag_snapshot(lines);
}

void network_diag_finish_unavailable(const char *ip_location_text)
{
    const char *lines[kNetworkDiagLineCount] = {
        network_diag_text(kNetworkDiagLocalIpPlaceholder),
        network_diag_text(kNetworkDiagPublicIpPlaceholder),
        ip_location_text ? network_diag_external_text(ip_location_text) :
                           network_diag_text(kNetworkDiagIpLocationFallback),
        network_diag_text(kNetworkDiagDnsUnchecked),
        network_diag_text(kNetworkDiagWeatherUnchecked),
        network_diag_text(kNetworkDiagNtpUnchecked),
        network_diag_text(kNetworkDiagSayingUnchecked),
        network_diag_text(kNetworkDiagInternetUnchecked),
        network_diag_text(kNetworkDiagOtaSourceUnchecked),
    };
    finish_network_diag_snapshot(lines);
}

namespace {
void network_diag_set_line(int index, const char *fmt, ...)
{
    if (!network_diag_line_index_valid(index)) {
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_INDEX_INVALID_FORMAT, index);
        return;
    }
    if (!fmt) {
        network_diag_clear_line(index);
        notify_ui_task();
        return;
    }
    char line[kNetworkDiagLineLen] = {};
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (written < 0) {
        line[0] = '\0';
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_FORMAT_FAILED_FORMAT, index);
    } else if (written >= kNetworkDiagLineLen) {
        line[kNetworkDiagLineLen - 1] = '\0';
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_TRUNCATED_FORMAT, index, written);
    }
    network_diag_line_store(index, line);
    notify_ui_task();
}

void network_diag_set_result_line(int index, const char *fmt, bool ok)
{
    network_diag_set_line(index, fmt, diag_result_text(ok));
}

void network_diag_set_checking_line(int index, const char *fmt)
{
    network_diag_set_line(index, fmt, network_diag_text(kNetworkDiagStatusChecking));
}

void network_diag_record_result_line(int index, const char *fmt, bool ok)
{
    network_diag_set_result_line(index, fmt, ok);
}

void network_diag_record_text_line(int index, const char *fmt, bool ok, const char *success_text, const char *failed_text)
{
    network_diag_set_line(index, fmt, ok ? success_text : failed_text);
}
} // namespace

bool run_network_diagnostic_checks(uint32_t request_generation)
{
    bool completed = false;
    if (!network_diagnostics_should_continue(kNetworkDiagLocalIpLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    char location[kNetworkDiagLocationTextSize] = {};
    char city[kNetworkDiagCityTextSize] = {};
    char public_ip[kNetworkDiagPublicIpTextSize] = {};
    char local_ip[kWifiStationIpTextLen] = {};
    bool local_ip_ok = wifi_station_ip_snapshot(local_ip, sizeof(local_ip));
    network_diag_record_text_line(kNetworkDiagLocalIpLine,
                                  network_diag_text(kNetworkDiagLocalIpFormat),
                                  local_ip_ok,
                                  local_ip,
                                  ui_text(UiTextId::UiPlaceholder));
    if (!network_diagnostics_should_continue(kNetworkDiagPublicIpLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    network_diag_set_checking_line(kNetworkDiagPublicIpLine,
                                   network_diag_text(kNetworkDiagPublicIpFormat));
    const NetworkDiagnosticPublicIpLookupResult public_ip_result =
        network_diagnostic_lookup_public_ip(
            kNetworkDiagPublicIpUrl,
            public_ip,
            sizeof(public_ip),
            kNetworkDiagPublicIpResponseBufferSize);
    bool public_ip_ok = public_ip_result.address_ok;
    if (!network_diagnostics_should_continue(kNetworkDiagPublicIpLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_text_line(kNetworkDiagPublicIpLine,
                                  network_diag_text(kNetworkDiagPublicIpFormat),
                                  public_ip_ok,
                                  public_ip,
                                  network_diag_text(kNetworkDiagStatusFailed));
    if (!network_diagnostics_should_continue(kNetworkDiagIpLocationLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    network_diag_set_checking_line(kNetworkDiagIpLocationLine,
                                   network_diag_text(kNetworkDiagIpLocationFormat));
    bool ip_ok = ip_geolocation_lookup(location, sizeof(location), city, sizeof(city));
    if (!network_diagnostics_should_continue(kNetworkDiagIpLocationLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_set_line(kNetworkDiagIpLocationLine,
                          network_diag_text(kNetworkDiagIpLocationCityFormat),
                          diag_result_text(ip_ok),
                          city[0] ? city : ui_text(UiTextId::UiPlaceholder));
    if (!network_diagnostics_should_continue(kNetworkDiagDnsLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    network_diag_set_checking_line(kNetworkDiagDnsLine,
                                   network_diag_text(kNetworkDiagDnsFormat));
    bool dns_ok = network_diagnostic_dns_lookup_ok(kNetworkDiagForecastDnsHost) &&
                  network_diagnostic_dns_lookup_ok(kNetworkDiagGeocodingDnsHost) &&
                  network_diagnostic_dns_lookup_ok(kNetworkDiagAirDnsHost) &&
                  network_diagnostic_dns_lookup_ok(kNetworkDiagGithubDnsHost);
    if (!network_diagnostics_should_continue(kNetworkDiagDnsLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagDnsLine,
                                    network_diag_text(kNetworkDiagDnsFormat),
                                    dns_ok);
    if (!network_diagnostics_should_continue(kNetworkDiagWeatherLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    bool weather_ok = false;
    if (network_weather_configuration_configured() &&
        !battery_low_mode_load()) {
        network_diag_set_checking_line(kNetworkDiagWeatherLine,
                                       network_diag_text(kNetworkDiagWeatherFormat));
        weather_ok =
            perform_weather_update(WeatherUpdateScope::kFull) ==
            WeatherUpdateResult::kSuccess;
    }
    if (!network_diagnostics_should_continue(kNetworkDiagWeatherLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagWeatherLine,
                                    network_diag_text(kNetworkDiagWeatherFormat),
                                    weather_ok);
    if (!network_diagnostics_should_continue(kNetworkDiagNtpLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_set_checking_line(kNetworkDiagNtpLine,
                                   network_diag_text(kNetworkDiagNtpFormat));
    bool ntp_ok = perform_ntp_sync(kNetworkDiagNtpMaxRetries);
    if (!network_diagnostics_should_continue(kNetworkDiagNtpLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagNtpLine,
                                    network_diag_text(kNetworkDiagNtpFormat),
                                    ntp_ok);
    if (!network_diagnostics_should_continue(kNetworkDiagSayingLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    network_diag_set_checking_line(kNetworkDiagSayingLine,
                                   network_diag_text(kNetworkDiagSayingFormat));
    bool saying_ok = !battery_low_mode_load() && perform_daily_saying_update();
    if (!network_diagnostics_should_continue(kNetworkDiagSayingLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagSayingLine,
                                    network_diag_text(kNetworkDiagSayingFormat),
                                    saying_ok);
    if (!network_diagnostics_should_continue(kNetworkDiagInternetLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_set_checking_line(kNetworkDiagInternetLine,
                                   network_diag_text(kNetworkDiagInternetFormat));
    bool internet_ok = public_ip_result.request_ok ||
                       network_diagnostic_http_probe_ok(
                           kNetworkDiagPublicIpUrl,
                           kNetworkDiagWideProbeBufferSize);
    if (!network_diagnostics_should_continue(kNetworkDiagInternetLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagInternetLine,
                                    network_diag_text(kNetworkDiagInternetFormat),
                                    internet_ok);
    if (!network_diagnostics_should_continue(kNetworkDiagOtaLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }

    network_diag_set_checking_line(kNetworkDiagOtaLine,
                                   network_diag_text(kNetworkDiagOtaFormat));
    bool ota_ok = network_diagnostic_http_probe_ok(
        kOtaManifestUrl,
        kNetworkDiagWideProbeBufferSize);
    if (!network_diagnostics_should_continue(kNetworkDiagOtaLine,
                                             completed,
                                             request_generation)) {
        return completed;
    }
    network_diag_record_result_line(kNetworkDiagOtaLine,
                                    network_diag_text(kNetworkDiagOtaFormat),
                                    ota_ok);
    return true;
}
