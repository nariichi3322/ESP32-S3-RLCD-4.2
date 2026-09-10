// 生成配网页 HTML、Wi-Fi 扫描列表和保存结果页面。
#include "wifi_portal_pages.h"

#include "app_metadata.h"
#include "app_network_config.h"
#include "app_text_format.h"
#include "checked_size.h"
#include "network_credentials_state.h"
#include "manual_weather_city_state.h"
#include "ntp_services.h"
#include "scoped_heap_buffer.h"
#include "wifi_portal_html_text.h"
#include "wifi_portal_ui_assets.h"
#include "wifi_portal_state_internal.h"
#include "ui_i18n.h"
#include "ui_language.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint16_t kMaxListedApCount = 32;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalLongestHtmlEntitySize = sizeof("&quot;") - 1;
constexpr size_t kPortalEscapedSsidSize =
    (kPortalSubmitSsidFieldSize - 1) * kPortalLongestHtmlEntitySize + 1;
constexpr size_t kPortalEscapedNtpServerSize =
    (kNtpServerNameLen - 1) * kPortalLongestHtmlEntitySize + 1;
constexpr size_t kPortalEscapedWeatherCitySize =
    (kManualWeatherCityLen - 1) * kPortalLongestHtmlEntitySize + 1;
constexpr size_t kPortalSaveExtraTextSize = 220;
constexpr size_t kPortalRootHtmlSize = 24576;
constexpr size_t kPortalSaveResultHtmlSize = 12288;
constexpr const char *kPortalSectionCloseHtml = "</div></div></section>";
constexpr const char *kPortalHtmlContentType = "text/html; charset=utf-8";
constexpr const char *kPortalPollScriptFormat =
    "<script>function poll(){fetch('/status',{cache:'no-store'}).then(function(r){if(r.status===200){document.getElementById('save-state').textContent='%s';document.getElementById('save-title').textContent='%s';document.getElementById('save-body').textContent='%s';return;}if(r.status===409){location.replace('/');return;}setTimeout(poll,1000);}).catch(function(){setTimeout(poll,1200);});}setTimeout(poll,800);</script>";
constexpr const char *kPortalHttpStatusInternalError = "500 Internal Server Error";
constexpr const char *kPortalHttpStatusFound = "302 Found";
constexpr const char *kPortalHeaderLocation = "Location";
constexpr const char *kPortalHeaderCacheControl = "Cache-Control";
constexpr const char *kPortalHeaderConnection = "Connection";
constexpr const char *kPortalCacheNoStore = "no-store";
constexpr const char *kPortalConnectionClose = "close";
constexpr const char *kPortalHtmlHeadPrefixSimplified =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
constexpr const char *kPortalHtmlHeadPrefixTraditional =
    "<!doctype html><html lang='zh-TW'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
constexpr const char *kPortalHtmlHeadPrefixEnglish =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
constexpr const char *kPortalHtmlHeadPrefixJapanese =
    "<!doctype html><html lang='ja'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
#define PORTAL_HTML_APPEND_FAILED_LOG "setup html append failed"
#define PORTAL_HTML_TRUNCATED_FORMAT "setup html truncated buffer=%u"

static_assert(kMaxListedApCount > 0, "listed AP count must be positive");
static_assert(kPortalEscapedSsidSize > kPortalSubmitSsidFieldSize,
              "escaped SSID buffer must exceed submitted SSID field size");
static_assert(kPortalRootHtmlSize > kPortalSaveResultHtmlSize,
              "portal root HTML buffer must exceed save result buffer");
static_assert(kPortalSaveExtraTextSize > 1, "portal save extra text buffer must fit text and NUL");

struct PortalPageTextWorkspace {
    char wifi_ssid[kNetworkWifiSsidLen];
    char backup_wifi_ssid[kNetworkWifiSsidLen];
    char safe_ssid[kPortalEscapedSsidSize];
    char safe_backup_ssid[kPortalEscapedSsidSize];
    char ntp_server[kNtpServerNameLen];
    char safe_ntp_server[kPortalEscapedNtpServerSize];
    char weather_city[kManualWeatherCityLen];
    char safe_weather_city[kPortalEscapedWeatherCitySize];
    char safe_extra[kPortalSaveExtraTextSize];
    char setup_ap_ssid[kWifiSetupApSsidTextLen];
};

// Synchronous URI handlers share the single HTTP server task, so page rendering
// can borrow one private workspace without adding allocation failure paths.
EXT_RAM_BSS_ATTR PortalPageTextWorkspace s_portal_page_text_workspace;
static_assert(sizeof(s_portal_page_text_workspace.safe_extra) ==
                  kPortalSaveExtraTextSize,
              "portal page text workspace must preserve extra-message capacity");

PortalPageTextWorkspace &reset_portal_page_text_workspace()
{
    memset(&s_portal_page_text_workspace,
           0,
           sizeof(s_portal_page_text_workspace));
    return s_portal_page_text_workspace;
}

void set_portal_common_response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, kPortalHeaderCacheControl, kPortalCacheNoStore);
    httpd_resp_set_hdr(req, kPortalHeaderConnection, kPortalConnectionClose);
}
esp_err_t send_portal_html(httpd_req_t *req, const char *html)
{
    if (!req || !html) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_type(req, kPortalHtmlContentType);
    set_portal_common_response_headers(req);
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_portal_empty_response(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    set_portal_common_response_headers(req);
    return httpd_resp_send(req, "", 0);
}

const char *portal_save_result_title(WifiPortalSaveResult result)
{
    switch (result) {
    case WifiPortalSaveResult::kSuccess:
        return ui_text(UiTextId::PortalSaveConnectedTitle);
    case WifiPortalSaveResult::kValidating:
        return ui_text(UiTextId::PortalSaveValidatingTitle);
    case WifiPortalSaveResult::kWifiConnectionFailed:
        return ui_text(UiTextId::PortalSaveWifiFailedTitle);
    case WifiPortalSaveResult::kWeatherApiFailed:
        return ui_text(UiTextId::PortalSaveWeatherApiFailedTitle);
    case WifiPortalSaveResult::kWeatherCityInvalid:
        return ui_text(UiTextId::PortalSaveWeatherCityInvalidTitle);
    case WifiPortalSaveResult::kNone:
    case WifiPortalSaveResult::kInvalidInput:
    default:
        return ui_text(UiTextId::PortalSaveMissingTitle);
    }
}

const char *portal_save_result_body(WifiPortalSaveResult result)
{
    switch (result) {
    case WifiPortalSaveResult::kSuccess:
        return ui_text(UiTextId::PortalSaveConnectedBody);
    case WifiPortalSaveResult::kValidating:
        return ui_text(UiTextId::PortalSaveValidatingBody);
    case WifiPortalSaveResult::kWifiConnectionFailed:
        return ui_text(UiTextId::PortalSaveWifiFailedBody);
    case WifiPortalSaveResult::kWeatherApiFailed:
        return ui_text(UiTextId::PortalSaveWeatherApiFailedBody);
    case WifiPortalSaveResult::kWeatherCityInvalid:
        return ui_text(UiTextId::PortalSaveWeatherCityInvalidBody);
    case WifiPortalSaveResult::kNone:
    case WifiPortalSaveResult::kInvalidInput:
    default:
        return ui_text(UiTextId::PortalSaveMissingBody);
    }
}

bool portal_save_result_is_visible(WifiPortalSaveResult result)
{
    return result != WifiPortalSaveResult::kNone;
}

void append_wifi_scan_message(char *html, size_t html_len, const char *message)
{
    html_append(html, html_len, "<p class='muted'>%s</p>", message ? message : "");
}

void append_wifi_scan_message_and_close(char *html, size_t html_len, const char *message)
{
    append_wifi_scan_message(html, html_len, message);
    html_append(html, html_len, kPortalSectionCloseHtml);
}
} // namespace

void html_append(char *html, size_t html_len, const char *fmt, ...)
{
    if (!app_text::output_buffer_available(html, html_len) || !fmt) {
        return;
    }
    size_t used = strnlen(html, html_len);
    if (used >= html_len - 1) {
        html[html_len - 1] = '\0';
        return;
    }
    size_t remaining = html_len - used;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(html + used, remaining, fmt, args);
    va_end(args);
    if (written < 0) {
        html[used] = '\0';
        ESP_LOGW(TAG, PORTAL_HTML_APPEND_FAILED_LOG);
    } else if (written >= (int)remaining) {
        html[html_len - 1] = '\0';
        ESP_LOGW(TAG, PORTAL_HTML_TRUNCATED_FORMAT, (unsigned)html_len);
    }
}

esp_err_t send_portal_text_status(httpd_req_t *req, const char *status, const char *text)
{
    if (!req || !status || !text) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_status(req, status);
    set_portal_common_response_headers(req);
    return httpd_resp_sendstr(req, text);
}

esp_err_t send_portal_empty_status(httpd_req_t *req, const char *status)
{
    if (!req || !status) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_status(req, status);
    return send_portal_empty_response(req);
}

esp_err_t redirect_to_setup_portal(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_status(req, kPortalHttpStatusFound);
    httpd_resp_set_hdr(req, kPortalHeaderLocation, kSetupPortalUrl);
    httpd_resp_set_hdr(req, kPortalHeaderCacheControl, kPortalCacheNoStore);
    return send_portal_empty_response(req);
}

void append_wifi_scan_list(char *html, size_t html_len)
{
    if (!app_text::output_buffer_available(html, html_len)) {
        return;
    }
    html_append(html,
                html_len,
                "<section class='wifi-section portal-panel'><div class='portal-panel-body'><div class='section-title'><span>%s</span><a href='/'>%s</a></div><div class='wifi-list'>",
                ui_text(UiTextId::PortalNearbyWifi),
                ui_text(UiTextId::PortalScanAgain));
    wifi_scan_config_t scan_config = {};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        append_wifi_scan_message(html, html_len,
                                 ui_text(UiTextId::PortalWifiScanBusy));
    } else {
        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len,
                ui_text(UiTextId::PortalWifiScanFailed));
            return;
        }
        if (ap_count == 0) {
            append_wifi_scan_message_and_close(html, html_len,
                ui_text(UiTextId::PortalWifiScanEmpty));
            return;
        }
        uint16_t max_records = ap_count;
        if (max_records > kMaxListedApCount) {
            max_records = kMaxListedApCount;
        }
        size_t records_bytes = 0;
        if (!app_memory::checked_size_multiply(max_records,
                                               sizeof(wifi_ap_record_t),
                                               &records_bytes)) {
            append_wifi_scan_message_and_close(html, html_len,
                ui_text(UiTextId::PortalWifiScanNoMemory));
            return;
        }
        ScopedHeapBuffer<uint8_t> records_storage(records_bytes,
                                                  HeapBufferInit::kZeroed,
                                                  HeapBufferStorage::kPsramPreferred);
        if (!records_storage) {
            append_wifi_scan_message_and_close(html, html_len,
                ui_text(UiTextId::PortalWifiScanNoMemory));
            return;
        }
        wifi_ap_record_t *records =
            reinterpret_cast<wifi_ap_record_t *>(records_storage.data());
        uint16_t record_count = max_records;
        err = esp_wifi_scan_get_ap_records(&record_count, records);
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len,
                ui_text(UiTextId::PortalWifiScanFailed));
            return;
        }
        if (record_count > max_records) {
            record_count = max_records;
        }
        if (record_count == 0) {
            append_wifi_scan_message(html, html_len,
                ui_text(UiTextId::PortalWifiScanEmpty));
        }
        for (uint16_t i = 0; i < record_count; ++i) {
            if (records[i].ssid[0] == '\0') {
                continue;
            }
            char ssid[kPortalEscapedSsidSize] = {};
            wifi_portal_html::escape_text(
                reinterpret_cast<const char *>(records[i].ssid),
                ssid,
                sizeof(ssid));
            html_append(html, html_len,
                        "<button type='button' class='wifi' data-ssid=\"%s\" onclick=\"pick(this.dataset.ssid)\"><span>%s</span><b>%d dBm</b></button>",
                        ssid, ssid, records[i].rssi);
        }
    }
    html_append(html, html_len, kPortalSectionCloseHtml);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    PortalPageTextWorkspace &text = reset_portal_page_text_workspace();
    (void)get_ntp_server_name(text.ntp_server, sizeof(text.ntp_server));
    (void)manual_weather_city_snapshot(text.weather_city,
                                       sizeof(text.weather_city));
    (void)network_wifi_ssid_snapshot(text.wifi_ssid,
                                     sizeof(text.wifi_ssid));
    (void)network_wifi_alternate_ssid_snapshot(
        text.backup_wifi_ssid, sizeof(text.backup_wifi_ssid));
    (void)wifi_setup_ap_ssid_snapshot(text.setup_ap_ssid,
                                      sizeof(text.setup_ap_ssid));
    wifi_portal_html::escape_text(
        text.wifi_ssid, text.safe_ssid, sizeof(text.safe_ssid));
    wifi_portal_html::escape_text(text.backup_wifi_ssid,
                                  text.safe_backup_ssid,
                                  sizeof(text.safe_backup_ssid));
    wifi_portal_html::escape_text(text.ntp_server,
                                  text.safe_ntp_server,
                                  sizeof(text.safe_ntp_server));
    wifi_portal_html::escape_text(text.weather_city,
                                  text.safe_weather_city,
                                  sizeof(text.safe_weather_city));
    const WifiPortalSaveSnapshot save =
        wifi_portal_save_snapshot_load();
    const WifiPortalSaveResult save_result = save.result;
    const bool show_feedback = portal_save_result_is_visible(save_result);
    const char *feedback_open = save_result == WifiPortalSaveResult::kValidating
                                    ? "<div class='feedback pending' role='status'><strong>"
                                    : (save_result == WifiPortalSaveResult::kSuccess
                                           ? "<div class='feedback success' role='status'><strong>"
                                           : "<div class='feedback' role='alert'><strong>");
    ScopedHeapBuffer<char> html(kPortalRootHtmlSize,
                                HeapBufferInit::kZeroed,
                                HeapBufferStorage::kPsramRequired);
    if (!html) {
        return send_portal_text_status(
            req, kPortalHttpStatusInternalError,
            ui_text(UiTextId::PortalErrorNotEnoughMemory));
    }
    char common_script_buffer[1024] = {};
    snprintf(common_script_buffer,
             sizeof(common_script_buffer),
             wifi_portal_ui::kCommonScript,
             ui_text(UiTextId::PortalSaving));
    html_append(html.data(), html.size(),
                "%s"
                "<title>%s</title><style>%s</style><script>%s</script></head>"
                "<body><main class='portal-shell'><header class='portal-header'><div class='brand-lockup'><div class='brand-mark'>42</div><div class='brand-copy'><h1>%s</h1><p>%s</p></div></div>"
                "<div class='ap-meta'><span>%s</span><strong>%s</strong></div></header>"
                "<section class='portal-form-shell'>%s%s%s",
                ui_language_is_traditional()
                    ? kPortalHtmlHeadPrefixTraditional
                    : ui_language_is_simplified()
                          ? kPortalHtmlHeadPrefixSimplified
                          : ui_language_is_japanese()
                                ? kPortalHtmlHeadPrefixJapanese
                                : kPortalHtmlHeadPrefixEnglish,
                ui_text(UiTextId::PortalClockSetupMode),
                wifi_portal_ui::kCommonCss,
                common_script_buffer,
                ui_text(UiTextId::PortalClockSetupMode),
                ui_text(UiTextId::PortalWifiNtpSetup),
                ui_text(UiTextId::PortalDeviceHotspot),
                text.setup_ap_ssid,
                show_feedback ? feedback_open : "",
                show_feedback ? portal_save_result_title(save_result) : "",
                show_feedback ? "</strong>" : "");
    if (show_feedback) {
        html_append(html.data(), html.size(), "%s</div>", portal_save_result_body(save_result));
    }
    html_append(html.data(), html.size(),
                wifi_portal_ui::kFormHtml,
                ui_text(UiTextId::PortalWifiSectionTitle),
                ui_text(UiTextId::PortalWifiSectionDescription),
                ui_text(UiTextId::PortalPrimaryWifiSsidLabel),
                ui_text(UiTextId::PortalPrimaryWifiSsidPlaceholder),
                text.safe_ssid,
                ui_text(UiTextId::PortalPrimaryWifiPasswordLabel),
                ui_text(UiTextId::PortalPrimaryWifiPasswordPlaceholder),
                ui_text(UiTextId::PortalPrimaryWifiPasswordHint),
                ui_text(UiTextId::PortalBackupWifiSsidLabel),
                ui_text(UiTextId::PortalOptional),
                ui_text(UiTextId::PortalBackupWifiSsidPlaceholder),
                text.safe_backup_ssid,
                ui_text(UiTextId::PortalBackupWifiPasswordLabel),
                ui_text(UiTextId::PortalOptional),
                ui_text(UiTextId::PortalBackupWifiPasswordPlaceholder),
                ui_text(UiTextId::PortalBackupWifiPasswordHint),
                ui_text(UiTextId::PortalTimeSyncTitle),
                ui_text(UiTextId::PortalTimeSyncDescription),
                ui_text(UiTextId::PortalNtpServer),
                ui_text(UiTextId::PortalNtpServerPlaceholder),
                text.safe_ntp_server,
                ui_text(UiTextId::PortalNtpServerHint),
                ui_text(UiTextId::PortalWeatherCityLabel),
                ui_text(UiTextId::PortalOptional),
                ui_text(UiTextId::PortalWeatherCityPlaceholder),
                text.safe_weather_city,
                ui_text(UiTextId::PortalWeatherCityHint),
                ui_text(UiTextId::PortalSaveAndConnect),
                ui_text(UiTextId::PortalSaveStatus),
                ui_text(UiTextId::PortalOfflineModeTitle),
                ui_text(UiTextId::PortalOfflineModeDescription),
                ui_text(UiTextId::PortalLocalDateTime),
                ui_text(UiTextId::PortalStartOfflineMode));

    html_append(html.data(), html.size(), "</section>");
    append_wifi_scan_list(html.data(), html.size());
    html_append(html.data(), html.size(), "</main></body></html>");
    esp_err_t err = send_portal_html(req, html.data());
    if (err == ESP_OK && save_result == WifiPortalSaveResult::kSuccess) {
        (void)wifi_portal_mark_save_feedback_seen(save);
    }
    return err;
}

esp_err_t send_save_result_page(httpd_req_t *req,
                                WifiPortalSaveResult result,
                                const char *extra_message)
{
    PortalPageTextWorkspace &text = reset_portal_page_text_workspace();
    (void)network_wifi_ssid_snapshot(text.wifi_ssid,
                                     sizeof(text.wifi_ssid));
    (void)network_wifi_alternate_ssid_snapshot(
        text.backup_wifi_ssid, sizeof(text.backup_wifi_ssid));
    (void)get_ntp_server_name(text.ntp_server, sizeof(text.ntp_server));
    wifi_portal_html::escape_text(text.wifi_ssid,
                                  text.safe_ssid,
                                  sizeof(text.safe_ssid));
    wifi_portal_html::escape_text(text.backup_wifi_ssid,
                                  text.safe_backup_ssid,
                                  sizeof(text.safe_backup_ssid));
    wifi_portal_html::escape_text(text.ntp_server,
                                  text.safe_ntp_server,
                                  sizeof(text.safe_ntp_server));
    wifi_portal_html::escape_text(extra_message ? extra_message : "",
                                  text.safe_extra,
                                  sizeof(text.safe_extra));
    ScopedHeapBuffer<char> html(kPortalSaveResultHtmlSize,
                                HeapBufferInit::kZeroed,
                                HeapBufferStorage::kPsramRequired);
    if (!html) {
        return send_portal_text_status(
            req, kPortalHttpStatusInternalError,
            ui_text(UiTextId::PortalErrorNotEnoughMemory));
    }
    const char *title = portal_save_result_title(result);
    const char *body = portal_save_result_body(result);
    char poll_script_buffer[1536] = {};
    if (result == WifiPortalSaveResult::kValidating) {
        snprintf(poll_script_buffer,
                 sizeof(poll_script_buffer),
                 kPortalPollScriptFormat,
                 ui_text(UiTextId::PortalConnected),
                 ui_text(UiTextId::PortalSetupCompleteTitle),
                 ui_text(UiTextId::PortalReturningToClock));
    }
    const char *poll_script = poll_script_buffer;
    const char *state_text = result == WifiPortalSaveResult::kSuccess
                                   ? ui_text(UiTextId::PortalConnected)
                                  : (result == WifiPortalSaveResult::kValidating
                                         ? ui_text(UiTextId::PortalValidating)
                                         : ui_text(UiTextId::PortalFailed));
    const char *meta_separator = ui_text(UiTextId::PortalSeparator);
    const int disconnect_reason = wifi_last_disconnect_reason();
    html_append(html.data(), html.size(),
                "%s"
                "<title>%s</title><style>%s</style>%s</head><body><main class='result-shell'><section class='portal-panel result-panel'><div id='save-state' class='result-state'>%s</div><h1 id='save-title'>%s</h1><p id='save-body'>%s</p>"
                "%s%s%s<div class='meta'>%s%s%s<br>%s%s%s<br>%s%s%s<br>%s%s%d</div><a class='primary-link' href='/'>%s</a></section></main></body></html>",
                ui_language_is_traditional()
                    ? kPortalHtmlHeadPrefixTraditional
                    : ui_language_is_simplified()
                          ? kPortalHtmlHeadPrefixSimplified
                          : ui_language_is_japanese()
                                ? kPortalHtmlHeadPrefixJapanese
                                : kPortalHtmlHeadPrefixEnglish,
                ui_text(UiTextId::PortalClockSetupResult),
                wifi_portal_ui::kCommonCss,
                poll_script,
                state_text,
                title,
                body,
                text.safe_extra[0] ? "<div class='note'>" : "",
                text.safe_extra,
                text.safe_extra[0] ? "</div>" : "",
                ui_text(UiTextId::PortalPrimaryWifi),
                meta_separator,
                text.safe_ssid,
                ui_text(UiTextId::PortalBackupWifi),
                meta_separator,
                text.safe_backup_ssid[0]
                    ? text.safe_backup_ssid
                    : ui_text(UiTextId::PortalNotConfigured),
                ui_text(UiTextId::PortalNtpServer),
                meta_separator,
                text.safe_ntp_server,
                ui_text(UiTextId::PortalLastWifiDisconnect),
                meta_separator,
                disconnect_reason,
                ui_text(UiTextId::PortalBackToSetup));
    return send_portal_html(req, html.data());
}

// End of Settings Mode portal page rendering.
