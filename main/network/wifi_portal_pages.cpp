// 生成配网页 HTML、Wi-Fi 扫描列表和保存结果页面。
#include "wifi_portal_pages.h"

#include "app_state.h"

#include <stdarg.h>

namespace {
constexpr uint16_t kMaxListedApCount = 32;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalWeatherCityNameSize = 32;
constexpr size_t kPortalEscapedSsidSize = 80;
constexpr size_t kPortalEscapedCitySize = 80;
constexpr size_t kPortalSaveExtraTextSize = 220;
constexpr size_t kPortalRootHtmlSize = 12288;
constexpr size_t kPortalSaveResultHtmlSize = 1700;
constexpr size_t kPortalOfflineResultHtmlSize = 1200;
constexpr const char *kPortalSectionCloseHtml = "</div></section>";
constexpr const char *kPortalHtmlContentType = "text/html; charset=utf-8";
constexpr const char *kPortalHttpStatusInternalError = "500 Internal Server Error";
constexpr const char *kPortalHttpStatusFound = "302 Found";
constexpr const char *kPortalHeaderLocation = "Location";
constexpr const char *kPortalHeaderCacheControl = "Cache-Control";
constexpr const char *kPortalCacheNoStore = "no-store";
constexpr const char *kPortalErrorNotEnoughMemory = "Not enough memory.";
constexpr const char *kPortalSaveConnectedTitle = "Connected";
constexpr const char *kPortalSaveConnectingTitle = "Saved, still connecting";
constexpr const char *kPortalSaveMissingTitle = "Missing setup data";
constexpr const char *kPortalSaveConnectedBody = "The clock has joined your Wi-Fi network.";
constexpr const char *kPortalSaveConnectingBody =
    "The clock saved your settings but did not get an IP yet. Check the password or router signal, then try again.";
constexpr const char *kPortalSaveMissingBody =
    "Enter Wi-Fi and QWeather API Key, or set date and time for offline mode.";
constexpr const char *kPortalOfflineSavedTitle = "Offline mode enabled";
constexpr const char *kPortalOfflineInvalidTitle = "Invalid date or time";
constexpr const char *kPortalOfflineSavedBody = "The clock will use the RTC time and skip all network updates.";
constexpr const char *kPortalOfflineInvalidBody = "Please enter a valid date and time, or configure Wi-Fi and API Key.";
constexpr const char *kPortalWifiScanBusyMessage = "Scan busy, refresh this page.";
constexpr const char *kPortalWifiScanFailedMessage = "Scan failed, refresh this page.";
constexpr const char *kPortalWifiScanEmptyMessage = "No Wi-Fi found.";
constexpr const char *kPortalWifiScanNoMemoryMessage = "Not enough memory to list Wi-Fi.";
constexpr const char *kPortalHtmlHeadPrefix =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
#define PORTAL_HTML_APPEND_FAILED_LOG "setup html append failed"
#define PORTAL_HTML_TRUNCATED_FORMAT "setup html truncated buffer=%u"

constexpr const char *kPortalPageTexts[] = {
    kPortalSectionCloseHtml,
    kPortalHtmlContentType,
    kPortalHttpStatusInternalError,
    kPortalHttpStatusFound,
    kPortalHeaderLocation,
    kPortalHeaderCacheControl,
    kPortalCacheNoStore,
    kPortalErrorNotEnoughMemory,
    kPortalSaveConnectedTitle,
    kPortalSaveConnectingTitle,
    kPortalSaveMissingTitle,
    kPortalSaveConnectedBody,
    kPortalSaveConnectingBody,
    kPortalSaveMissingBody,
    kPortalOfflineSavedTitle,
    kPortalOfflineInvalidTitle,
    kPortalOfflineSavedBody,
    kPortalOfflineInvalidBody,
    kPortalWifiScanBusyMessage,
    kPortalWifiScanFailedMessage,
    kPortalWifiScanEmptyMessage,
    kPortalWifiScanNoMemoryMessage,
    kPortalHtmlHeadPrefix,
    PORTAL_HTML_APPEND_FAILED_LOG,
    PORTAL_HTML_TRUNCATED_FORMAT,
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *text : items) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

static_assert(kMaxListedApCount > 0, "listed AP count must be positive");
static_assert(kPortalEscapedSsidSize > kPortalSubmitSsidFieldSize,
              "escaped SSID buffer must exceed submitted SSID field size");
static_assert(kPortalEscapedCitySize > kPortalWeatherCityNameSize,
              "escaped city buffer must exceed weather city name buffer");
static_assert(kPortalRootHtmlSize > kPortalSaveResultHtmlSize,
              "portal root HTML buffer must exceed save result buffer");
static_assert(kPortalRootHtmlSize > kPortalOfflineResultHtmlSize,
              "portal root HTML buffer must exceed offline result buffer");
static_assert(kPortalSaveExtraTextSize > 1, "portal save extra text buffer must fit text and NUL");
static_assert(array_count(kPortalPageTexts) > 0, "portal page text registry must not be empty");
static_assert(cstr_array_nonempty(kPortalPageTexts), "portal page texts must be non-empty");

bool portal_output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

class WifiScanRecords {
public:
    explicit WifiScanRecords(uint16_t count)
        : records_((wifi_ap_record_t *)calloc(count, sizeof(wifi_ap_record_t))),
          capacity_(count)
    {
    }

    ~WifiScanRecords()
    {
        free(records_);
    }

    WifiScanRecords(const WifiScanRecords &) = delete;
    WifiScanRecords &operator=(const WifiScanRecords &) = delete;

    wifi_ap_record_t *data() const
    {
        return records_;
    }

    uint16_t capacity() const
    {
        return capacity_;
    }

    uint16_t count() const
    {
        return count_;
    }

    explicit operator bool() const
    {
        return records_ != nullptr;
    }

    void set_count(uint16_t count)
    {
        count_ = count <= capacity_ ? count : capacity_;
    }

private:
    wifi_ap_record_t *records_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t count_ = 0;
};

class PortalHtmlBuffer {
public:
    explicit PortalHtmlBuffer(size_t len)
        : html_((char *)calloc(1, len)),
          size_(len)
    {
    }

    ~PortalHtmlBuffer()
    {
        free(html_);
    }

    PortalHtmlBuffer(const PortalHtmlBuffer &) = delete;
    PortalHtmlBuffer &operator=(const PortalHtmlBuffer &) = delete;

    char *data() const
    {
        return html_;
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return html_ != nullptr;
    }

private:
    char *html_ = nullptr;
    size_t size_ = 0;
};

esp_err_t send_portal_html(httpd_req_t *req, const char *html)
{
    if (!req || !html) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_type(req, kPortalHtmlContentType);
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_portal_empty_response(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    return httpd_resp_send(req, "", 0);
}

const char *portal_save_result_title(bool saved, bool connected)
{
    if (!saved) {
        return kPortalSaveMissingTitle;
    }
    return connected ? kPortalSaveConnectedTitle : kPortalSaveConnectingTitle;
}

const char *portal_save_result_body(bool saved, bool connected)
{
    if (!saved) {
        return kPortalSaveMissingBody;
    }
    return connected ? kPortalSaveConnectedBody : kPortalSaveConnectingBody;
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
    if (!portal_output_buffer_available(html, html_len) || !fmt) {
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

void html_escape(const char *src, char *dst, size_t dst_len)
{
    if (!portal_output_buffer_available(dst, dst_len)) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_len; ++si) {
        const char *rep = nullptr;
        if (src[si] == '&') {
            rep = "&amp;";
        } else if (src[si] == '<') {
            rep = "&lt;";
        } else if (src[si] == '>') {
            rep = "&gt;";
        } else if (src[si] == '"') {
            rep = "&quot;";
        }
        if (rep) {
            size_t rep_len = strlen(rep);
            if (di + rep_len >= dst_len) {
                break;
            }
            memcpy(dst + di, rep, rep_len);
            di += rep_len;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

esp_err_t send_portal_text_status(httpd_req_t *req, const char *status, const char *text)
{
    if (!req || !status || !text) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_resp_set_status(req, status);
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
    if (!portal_output_buffer_available(html, html_len)) {
        return;
    }
    html_append(html, html_len, "<section><div class='section-title'><span>Nearby Wi-Fi</span><a href='/'>Refresh</a></div><div class='wifi-list'>");
    wifi_scan_config_t scan_config = {};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        append_wifi_scan_message(html, html_len, kPortalWifiScanBusyMessage);
    } else {
        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanFailedMessage);
            return;
        }
        if (ap_count == 0) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanEmptyMessage);
            return;
        }
        uint16_t max_records = ap_count;
        if (max_records > kMaxListedApCount) {
            max_records = kMaxListedApCount;
        }
        WifiScanRecords records(max_records);
        if (!records) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanNoMemoryMessage);
            return;
        }
        uint16_t record_count = records.capacity();
        err = esp_wifi_scan_get_ap_records(&record_count, records.data());
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanFailedMessage);
            return;
        }
        records.set_count(record_count);
        if (records.count() == 0) {
            append_wifi_scan_message(html, html_len, kPortalWifiScanEmptyMessage);
        }
        for (uint16_t i = 0; i < records.count(); ++i) {
            if (records.data()[i].ssid[0] == '\0') {
                continue;
            }
            char ssid[kPortalEscapedSsidSize] = {};
            html_escape((const char *)records.data()[i].ssid, ssid, sizeof(ssid));
            html_append(html, html_len,
                        "<button type='button' class='wifi' data-ssid=\"%s\" onclick=\"pick(this.dataset.ssid)\"><span>%s</span><b>%d dBm</b></button>",
                        ssid, ssid, records.data()[i].rssi);
        }
    }
    html_append(html, html_len, kPortalSectionCloseHtml);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_weather_city[kPortalEscapedCitySize] = {};
    html_escape(g_wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(g_manual_weather_city, safe_weather_city, sizeof(safe_weather_city));
    PortalHtmlBuffer html(kPortalRootHtmlSize);
    if (!html) {
        return send_portal_text_status(req, kPortalHttpStatusInternalError, kPortalErrorNotEnoughMemory);
    }
    html_append(html.data(), html.size(),
                "%s"
                "<title>WeatherClock Setup</title><style>"
                ":root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:480px;margin:0 auto;padding:22px 16px 34px}.brand{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}"
                ".mark{width:44px;height:44px;border:2px solid #17202a;border-radius:8px;display:grid;place-items:center;font-weight:900;font-size:22px;background:#fff}"
                ".pill{border:1px solid #b7c0ca;border-radius:999px;padding:7px 10px;font-size:12px;color:#465563;background:#fff}"
                "h1{font-size:26px;line-height:1.12;margin:0 0 4px}p{margin:0}.sub{font-size:14px;color:#5d6b78}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:16px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                "label{display:block;font-size:12px;font-weight:700;letter-spacing:.03em;color:#465563;margin:13px 0 6px;text-transform:uppercase}"
                "input{width:100%;height:46px;border:1px solid #aeb8c2;border-radius:6px;padding:0 12px;font-size:17px;background:#fbfcfd;color:#111;outline:none}"
                "input:focus{border-color:#17202a;box-shadow:0 0 0 3px rgba(23,32,42,.10)}.submit{width:100%;height:48px;border:0;border-radius:6px;margin-top:16px;background:#17202a;color:#fff;font-size:17px;font-weight:800}"
                "section{margin-top:16px}.section-title{display:flex;align-items:center;justify-content:space-between;margin:0 2px 8px;font-size:13px;font-weight:800;color:#465563}.section-title a{color:#17202a;text-decoration:none}"
                ".wifi-list{display:grid;gap:8px}.wifi{width:100%;border:1px solid #d3dae2;background:#fff;border-radius:6px;padding:12px;display:flex;justify-content:space-between;gap:12px;text-align:left;font-size:16px;color:#17202a}"
                ".wifi b{font-size:12px;color:#697784;white-space:nowrap}.muted{padding:12px;border:1px dashed #c7d0d9;border-radius:6px;color:#697784;background:#fbfcfd}"
                "</style><script>function pick(s){document.querySelector('[name=ssid]').value=s;document.querySelector('[name=pass]').focus();}</script></head>"
                "<body><main class='wrap'><div class='brand'><div><h1>WeatherClock</h1><p class='sub'>Connect Wi-Fi, or set time for offline mode.</p></div><div class='mark'>42</div></div>"
                "<div class='panel'><div class='pill'>Setup AP: %s</div><form method='get' action='/save' accept-charset='UTF-8'>"
                "<label>Wi-Fi SSID</label><input name='ssid' placeholder='Choose or type network name' value='%s' autocomplete='off'>"
                "<label>Password</label><input name='pass' placeholder='Wi-Fi password' type='password' autocomplete='current-password'>"
                "<label>QWeather API Key</label><input name='api_key' placeholder='Leave blank to keep saved key' value='' autocomplete='off'>"
                "<label>Weather City (Optional)</label><input name='weather_city' placeholder='Leave blank for auto location' value='%s' autocomplete='off'>"
                "<label>Offline Date & Time</label><input name='manual_time' type='datetime-local' placeholder='Set time without Wi-Fi'>"
                "<button class='submit' type='submit'>Save and connect</button></form></div>",
                kPortalHtmlHeadPrefix, g_ap_ssid, safe_ssid, safe_weather_city);
    append_wifi_scan_list(html.data(), html.size());
    html_append(html.data(), html.size(), "</main></body></html>");
    return send_portal_html(req, html.data());
}

esp_err_t send_save_result_page(httpd_req_t *req, bool saved, bool connected, const char *extra_message)
{
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_city[kPortalEscapedCitySize] = {};
    char safe_extra[kPortalSaveExtraTextSize] = {};
    html_escape(g_wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(g_has_manual_weather_city ? g_manual_weather_city : "Auto", safe_city, sizeof(safe_city));
    html_escape(extra_message ? extra_message : "", safe_extra, sizeof(safe_extra));
    char html[kPortalSaveResultHtmlSize] = {};
    const char *title = portal_save_result_title(saved, connected);
    const char *body = portal_save_result_body(saved, connected);
    html_append(html, sizeof(html),
                "%s"
                "<title>WeatherClock Setup</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:48px;height:48px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:24px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}.note{border:1px solid #d3dae2;border-radius:6px;padding:10px;margin:0 0 14px;color:#17202a;background:#fbfcfd;font-size:14px}.meta{border-top:1px solid #e1e6eb;padding-top:12px;color:#697784;font-size:13px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style></head><body><main class='wrap'><section class='panel'><div class='state'>%s</div><h1>%s</h1><p>%s</p>"
                "%s%s%s<div class='meta'>SSID: %s<br>Weather city: %s<br>Last Wi-Fi reason: %d</div><a href='/'>Back to setup</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                connected ? "OK" : "!",
                title,
                body,
                safe_extra[0] ? "<div class='note'>" : "",
                safe_extra,
                safe_extra[0] ? "</div>" : "",
                safe_ssid,
                safe_city,
                g_last_wifi_disconnect_reason);
    return send_portal_html(req, html);
}

esp_err_t send_offline_result_page(httpd_req_t *req, bool saved)
{
    char html[kPortalOfflineResultHtmlSize] = {};
    html_append(html, sizeof(html),
                "%s"
                "<title>WeatherClock Offline</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:48px;height:48px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:24px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style></head><body><main class='wrap'><section class='panel'><div class='state'>%s</div><h1>%s</h1><p>%s</p><a href='/'>Back to setup</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                saved ? "OK" : "!",
                saved ? kPortalOfflineSavedTitle : kPortalOfflineInvalidTitle,
                saved ? kPortalOfflineSavedBody : kPortalOfflineInvalidBody);
    return send_portal_html(req, html);
}
