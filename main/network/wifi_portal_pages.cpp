// 生成配网页 HTML、Wi-Fi 扫描列表和保存结果页面。
#include "wifi_portal_pages.h"

#include "app_metadata.h"
#include "app_network_config.h"
#include "app_text_format.h"
#include "checked_size.h"
#include "manual_weather_city_state.h"
#include "network_credentials_state.h"
#include "scoped_heap_buffer.h"
#include "wifi_portal_state.h"

#include "esp_log.h"
#include "esp_wifi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint16_t kMaxListedApCount = 32;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalWeatherCityNameSize = 32;
constexpr size_t kPortalEscapedSsidSize = 80;
constexpr size_t kPortalEscapedCitySize = 80;
constexpr size_t kPortalSaveExtraTextSize = 220;
constexpr size_t kPortalRootHtmlSize = 12288;
constexpr size_t kPortalSaveResultHtmlSize = 2800;
constexpr size_t kPortalOfflineResultHtmlSize = 1200;
constexpr const char *kPortalSectionCloseHtml = "</div></section>";
constexpr const char *kPortalHtmlContentType = "text/html; charset=utf-8";
constexpr const char *kPortalHttpStatusInternalError = "500 Internal Server Error";
constexpr const char *kPortalHttpStatusFound = "302 Found";
constexpr const char *kPortalHeaderLocation = "Location";
constexpr const char *kPortalHeaderCacheControl = "Cache-Control";
constexpr const char *kPortalHeaderConnection = "Connection";
constexpr const char *kPortalCacheNoStore = "no-store";
constexpr const char *kPortalConnectionClose = "close";
constexpr const char *kPortalErrorNotEnoughMemory = "设备内存不足，请稍后重试。";
constexpr const char *kPortalSaveConnectedTitle = "网络连接成功";
constexpr const char *kPortalSaveValidatingTitle = "正在验证网络配置";
constexpr const char *kPortalSaveMissingTitle = "配置信息不完整";
constexpr const char *kPortalSaveWifiFailedTitle = "Wi-Fi 连接失败";
constexpr const char *kPortalSaveWeatherApiFailedTitle = "天气 API 验证失败";
constexpr const char *kPortalSaveWeatherCityInvalidTitle = "天气城市无效";
constexpr const char *kPortalSaveConnectedBody = "天气时钟已连接到 Wi-Fi 网络。";
constexpr const char *kPortalSaveValidatingBody =
    "设备正在连接 Wi-Fi，并验证天气 API 密钥和天气城市，请稍候。";
constexpr const char *kPortalSaveMissingBody =
    "请填写 Wi-Fi 和和风天气 API 密钥；如果只使用离线模式，也可以仅设置日期和时间。";
constexpr const char *kPortalSaveWifiFailedBody =
    "设备未能连接到该 Wi-Fi。请检查密码、信号和路由器状态后重新填写。";
constexpr const char *kPortalSaveWeatherApiFailedBody =
    "Wi-Fi 已连接，但和风天气 API 密钥无法使用。请检查密钥后重新填写。";
constexpr const char *kPortalSaveWeatherCityInvalidBody =
    "Wi-Fi 与 API 密钥可用，但和风天气无法识别该城市。请修改城市，或留空使用自动定位。";
constexpr const char *kPortalOfflineSavedTitle = "离线模式已开启";
constexpr const char *kPortalOfflineInvalidTitle = "日期或时间无效";
constexpr const char *kPortalOfflineSavedBody = "天气时钟将使用 RTC 时间，并停止所有网络更新。";
constexpr const char *kPortalOfflineInvalidBody = "请输入有效的日期和时间，或者填写 Wi-Fi 和和风天气 API 密钥。";
constexpr const char *kPortalWifiScanBusyMessage = "Wi-Fi 正在扫描，请稍后刷新页面。";
constexpr const char *kPortalWifiScanFailedMessage = "Wi-Fi 扫描失败，请刷新页面重试。";
constexpr const char *kPortalWifiScanEmptyMessage = "没有发现可用的 Wi-Fi 网络。";
constexpr const char *kPortalWifiScanNoMemoryMessage = "设备内存不足，暂时无法显示 Wi-Fi 列表。";
constexpr const char *kPortalHtmlHeadPrefix =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
#define PORTAL_HTML_APPEND_FAILED_LOG "setup html append failed"
#define PORTAL_HTML_TRUNCATED_FORMAT "setup html truncated buffer=%u"

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
        return kPortalSaveConnectedTitle;
    case WifiPortalSaveResult::kValidating:
        return kPortalSaveValidatingTitle;
    case WifiPortalSaveResult::kWifiConnectionFailed:
        return kPortalSaveWifiFailedTitle;
    case WifiPortalSaveResult::kWeatherApiFailed:
        return kPortalSaveWeatherApiFailedTitle;
    case WifiPortalSaveResult::kWeatherCityInvalid:
        return kPortalSaveWeatherCityInvalidTitle;
    case WifiPortalSaveResult::kNone:
    case WifiPortalSaveResult::kInvalidInput:
    default:
        return kPortalSaveMissingTitle;
    }
}

const char *portal_save_result_body(WifiPortalSaveResult result)
{
    switch (result) {
    case WifiPortalSaveResult::kSuccess:
        return kPortalSaveConnectedBody;
    case WifiPortalSaveResult::kValidating:
        return kPortalSaveValidatingBody;
    case WifiPortalSaveResult::kWifiConnectionFailed:
        return kPortalSaveWifiFailedBody;
    case WifiPortalSaveResult::kWeatherApiFailed:
        return kPortalSaveWeatherApiFailedBody;
    case WifiPortalSaveResult::kWeatherCityInvalid:
        return kPortalSaveWeatherCityInvalidBody;
    case WifiPortalSaveResult::kNone:
    case WifiPortalSaveResult::kInvalidInput:
    default:
        return kPortalSaveMissingBody;
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

void html_escape(const char *src, char *dst, size_t dst_len)
{
    if (!app_text::output_buffer_available(dst, dst_len)) {
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
    html_append(html, html_len, "<section><div class='section-title'><span>附近的 Wi-Fi</span><a href='/'>重新扫描</a></div><div class='wifi-list'>");
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
        size_t records_bytes = 0;
        if (!app_memory::checked_size_multiply(max_records,
                                               sizeof(wifi_ap_record_t),
                                               &records_bytes)) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanNoMemoryMessage);
            return;
        }
        ScopedHeapBuffer<uint8_t> records_storage(records_bytes,
                                                  HeapBufferInit::kZeroed);
        if (!records_storage) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanNoMemoryMessage);
            return;
        }
        wifi_ap_record_t *records =
            reinterpret_cast<wifi_ap_record_t *>(records_storage.data());
        uint16_t record_count = max_records;
        err = esp_wifi_scan_get_ap_records(&record_count, records);
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanFailedMessage);
            return;
        }
        if (record_count > max_records) {
            record_count = max_records;
        }
        if (record_count == 0) {
            append_wifi_scan_message(html, html_len, kPortalWifiScanEmptyMessage);
        }
        for (uint16_t i = 0; i < record_count; ++i) {
            if (records[i].ssid[0] == '\0') {
                continue;
            }
            char ssid[kPortalEscapedSsidSize] = {};
            html_escape((const char *)records[i].ssid, ssid, sizeof(ssid));
            html_append(html, html_len,
                        "<button type='button' class='wifi' data-ssid=\"%s\" onclick=\"pick(this.dataset.ssid)\"><span>%s</span><b>%d dBm</b></button>",
                        ssid, ssid, records[i].rssi);
        }
    }
    html_append(html, html_len, kPortalSectionCloseHtml);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    char wifi_ssid[kNetworkWifiSsidLen] = {};
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_weather_city[kPortalEscapedCitySize] = {};
    char weather_city[kManualWeatherCityLen] = {};
    char setup_ap_ssid[kWifiSetupApSsidTextLen] = {};
    (void)manual_weather_city_snapshot(weather_city, sizeof(weather_city));
    (void)network_wifi_ssid_snapshot(wifi_ssid, sizeof(wifi_ssid));
    (void)wifi_setup_ap_ssid_snapshot(setup_ap_ssid, sizeof(setup_ap_ssid));
    html_escape(wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(weather_city, safe_weather_city, sizeof(safe_weather_city));
    const WifiPortalSaveResult save_result = wifi_portal_save_result_load();
    const bool show_feedback = portal_save_result_is_visible(save_result);
    const char *feedback_open = save_result == WifiPortalSaveResult::kValidating
                                    ? "<div class='feedback pending' role='status'><strong>"
                                    : (save_result == WifiPortalSaveResult::kSuccess
                                           ? "<div class='feedback success' role='status'><strong>"
                                           : "<div class='feedback' role='alert'><strong>");
    ScopedHeapBuffer<char> html(kPortalRootHtmlSize,
                                HeapBufferInit::kZeroed,
                                HeapBufferStorage::kPsramPreferred);
    if (!html) {
        return send_portal_text_status(req, kPortalHttpStatusInternalError, kPortalErrorNotEnoughMemory);
    }
    html_append(html.data(), html.size(),
                "%s"
                "<title>天气时钟配网</title><style>"
                ":root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:480px;margin:0 auto;padding:22px 16px 34px}.brand{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}"
                ".mark{width:44px;height:44px;border:2px solid #17202a;border-radius:8px;display:grid;place-items:center;font-weight:900;font-size:22px;background:#fff}"
                ".pill{border:1px solid #b7c0ca;border-radius:999px;padding:7px 10px;font-size:12px;color:#465563;background:#fff}"
                "h1{font-size:26px;line-height:1.12;margin:0 0 4px}p{margin:0}.sub{font-size:14px;color:#5d6b78}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:16px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                "label{display:block;font-size:12px;font-weight:700;letter-spacing:.03em;color:#465563;margin:13px 0 6px;text-transform:uppercase}"
                "input{width:100%;height:46px;border:1px solid #aeb8c2;border-radius:6px;padding:0 12px;font-size:17px;background:#fbfcfd;color:#111;outline:none}"
                "input:focus{border-color:#17202a;box-shadow:0 0 0 3px rgba(23,32,42,.10)}.hint{margin:7px 1px 0;color:#697784;font-size:13px;line-height:1.4}.feedback{margin:14px 0 2px;padding:12px;border:2px solid #9a2f2f;border-radius:6px;background:#fff7f7;color:#651f1f;font-size:14px;line-height:1.45}.feedback.pending{border-color:#697784;background:#fbfcfd;color:#465563}.feedback.success{border-color:#2f6f4e;background:#f4fbf7;color:#24563d}.feedback strong{display:block;font-size:16px;margin-bottom:3px}.actions{display:block;margin-top:24px}.submit{display:block;width:100%;height:48px;border:0;border-radius:6px;margin:0;background:#17202a;color:#fff;font-size:17px;font-weight:800}.submit:disabled{opacity:.72}.save-status{display:none;margin:12px 0 0;padding:10px;border:1px solid #c7d0d9;border-radius:6px;background:#fbfcfd;color:#465563;font-size:14px}.save-status.show{display:block}"
                "section{margin-top:16px}.section-title{display:flex;align-items:center;justify-content:space-between;margin:0 2px 8px;font-size:13px;font-weight:800;color:#465563}.section-title a{color:#17202a;text-decoration:none}"
                ".wifi-list{display:grid;gap:8px}.wifi{width:100%;border:1px solid #d3dae2;background:#fff;border-radius:6px;padding:12px;display:flex;justify-content:space-between;gap:12px;text-align:left;font-size:16px;color:#17202a}"
                ".wifi b{font-size:12px;color:#697784;white-space:nowrap}.muted{padding:12px;border:1px dashed #c7d0d9;border-radius:6px;color:#697784;background:#fbfcfd}"
                "</style><script>function pick(s){document.querySelector('[name=ssid]').value=s;document.querySelector('[name=pass]').focus();}function beginSave(f){var b=f.querySelector('.submit'),m=document.getElementById('save-status');if(b){b.disabled=true;b.textContent='正在保存，请稍候…';}if(m){m.classList.add('show');}setTimeout(function(){f.submit();},80);return false;}</script></head>"
                "<body><main class='wrap'><div class='brand'><div><h1>天气时钟</h1><p class='sub'>连接 Wi-Fi 使用联网功能，或设置时间进入离线模式。</p></div><div class='mark'>42</div></div>"
                "<div class='panel'><div class='pill'>配网热点：%s</div>%s%s%s",
                kPortalHtmlHeadPrefix,
                setup_ap_ssid,
                show_feedback ? feedback_open : "",
                show_feedback ? portal_save_result_title(save_result) : "",
                show_feedback ? "</strong>" : "");
    if (show_feedback) {
        html_append(html.data(), html.size(), "%s</div>", portal_save_result_body(save_result));
    }
    html_append(html.data(), html.size(),
                "<form method='post' action='/save' accept-charset='UTF-8' onsubmit='return beginSave(this)'>"
                "<label>Wi-Fi 名称（SSID）</label><input name='ssid' placeholder='请选择或输入 Wi-Fi 名称' value='%s' autocomplete='off'>"
                "<label>Wi-Fi 密码</label><input name='pass' placeholder='请输入 Wi-Fi 密码' type='password' autocomplete='current-password'>"
                "<label>和风天气 API 密钥</label><input name='api_key' placeholder='已有密钥时可留空，继续使用原密钥' value='' autocomplete='off'>"
                "<label>天气城市（选填）</label><input name='weather_city' placeholder='例如：杭州；留空则根据公网 IP 自动定位' value='%s' autocomplete='off'>"
                "<label for='manual_time'>离线日期和时间（选填）</label><input id='manual_time' name='manual_time' type='datetime-local' placeholder='连接 Wi-Fi 时可留空' aria-describedby='manual-time-hint'>"
                "<p id='manual-time-hint' class='hint'>连接 Wi-Fi 时可以留空；仅离线使用时填写。</p>"
                "<div class='actions'><button class='submit' type='submit'>保存并连接</button></div><p id='save-status' class='save-status' role='status' aria-live='polite'>正在保存设置并连接，请稍候…</p></form></div>",
                safe_ssid,
                safe_weather_city);
    append_wifi_scan_list(html.data(), html.size());
    html_append(html.data(), html.size(), "</main></body></html>");
    esp_err_t err = send_portal_html(req, html.data());
    if (err == ESP_OK && save_result == WifiPortalSaveResult::kSuccess) {
        wifi_portal_save_feedback_seen_store(true);
    }
    return err;
}

esp_err_t send_save_result_page(httpd_req_t *req,
                                WifiPortalSaveResult result,
                                const char *extra_message)
{
    char wifi_ssid[kNetworkWifiSsidLen] = {};
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_city[kPortalEscapedCitySize] = {};
    char safe_extra[kPortalSaveExtraTextSize] = {};
    char weather_city[kManualWeatherCityLen] = {};
    const bool have_weather_city = manual_weather_city_snapshot(
        weather_city, sizeof(weather_city));
    (void)network_wifi_ssid_snapshot(wifi_ssid, sizeof(wifi_ssid));
    html_escape(wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(have_weather_city ? weather_city : "自动定位", safe_city, sizeof(safe_city));
    html_escape(extra_message ? extra_message : "", safe_extra, sizeof(safe_extra));
    ScopedHeapBuffer<char> html(kPortalSaveResultHtmlSize,
                                HeapBufferInit::kZeroed,
                                HeapBufferStorage::kPsramPreferred);
    if (!html) {
        return send_portal_text_status(req, kPortalHttpStatusInternalError, kPortalErrorNotEnoughMemory);
    }
    const char *title = portal_save_result_title(result);
    const char *body = portal_save_result_body(result);
    const char *poll_script = result == WifiPortalSaveResult::kValidating
                                  ? "<script>function poll(){fetch('/status',{cache:'no-store'}).then(function(r){if(r.status===200){document.getElementById('save-state').textContent='已连接';document.getElementById('save-title').textContent='网络连接成功';document.getElementById('save-body').textContent='验证通过，设备即将进入工作状态。';return;}if(r.status===409){location.replace('/');return;}setTimeout(poll,1000);}).catch(function(){setTimeout(poll,1200);});}setTimeout(poll,800);</script>"
                                  : "";
    const char *state_text = result == WifiPortalSaveResult::kSuccess
                                 ? "已连接"
                                 : (result == WifiPortalSaveResult::kValidating
                                        ? "验证中"
                                        : "失败");
    const int disconnect_reason = wifi_last_disconnect_reason();
    html_append(html.data(), html.size(),
                "%s"
                "<title>天气时钟配网结果</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:72px;height:42px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:16px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}.note{border:1px solid #d3dae2;border-radius:6px;padding:10px;margin:0 0 14px;color:#17202a;background:#fbfcfd;font-size:14px}.meta{border-top:1px solid #e1e6eb;padding-top:12px;color:#697784;font-size:13px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style>%s</head><body><main class='wrap'><section class='panel'><div id='save-state' class='state'>%s</div><h1 id='save-title'>%s</h1><p id='save-body'>%s</p>"
                "%s%s%s<div class='meta'>Wi-Fi 名称：%s<br>天气城市：%s<br>最近一次 Wi-Fi 断开原因：%d</div><a href='/'>返回配网页</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                poll_script,
                state_text,
                title,
                body,
                safe_extra[0] ? "<div class='note'>" : "",
                safe_extra,
                safe_extra[0] ? "</div>" : "",
                safe_ssid,
                safe_city,
                disconnect_reason);
    return send_portal_html(req, html.data());
}

esp_err_t send_offline_result_page(httpd_req_t *req, bool saved)
{
    ScopedHeapBuffer<char> html(kPortalOfflineResultHtmlSize,
                                HeapBufferInit::kZeroed,
                                HeapBufferStorage::kPsramPreferred);
    if (!html) {
        return send_portal_text_status(req, kPortalHttpStatusInternalError, kPortalErrorNotEnoughMemory);
    }
    html_append(html.data(), html.size(),
                "%s"
                "<title>天气时钟离线模式</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:72px;height:42px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:16px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style></head><body><main class='wrap'><section class='panel'><div class='state'>%s</div><h1>%s</h1><p>%s</p><a href='/'>返回配网页</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                saved ? "已开启" : "提示",
                saved ? kPortalOfflineSavedTitle : kPortalOfflineInvalidTitle,
                saved ? kPortalOfflineSavedBody : kPortalOfflineInvalidBody);
    return send_portal_html(req, html.data());
}
