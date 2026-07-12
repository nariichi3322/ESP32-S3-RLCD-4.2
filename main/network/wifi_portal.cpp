// 实现设备配网 AP、强制门户、Wi-Fi 扫描和网页保存流程。
#include "network_services.h"

#include "app_constexpr.h"
#include "app_text_format.h"
#include "wifi_portal_dns.h"
#include "wifi_portal_pages.h"

#include "audio_services.h"
#include "ui_views.h"
#include "xiaozhi_ai.h"

namespace {
esp_netif_t *s_ap_netif = nullptr;
constexpr uint8_t kSetupApChannel = 1;
constexpr uint8_t kSetupApMaxConnections = 4;
constexpr uint16_t kSetupHttpServerPort = 80;
constexpr size_t kSetupHttpServerStackSize = 8192;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalRequestBufferSize = 640;
constexpr size_t kPortalWeatherCityIdSize = 24;
constexpr size_t kPortalWeatherCityNameSize = 32;
constexpr uint32_t kPortalSaveWifiConnectWaitMs = 12000;
constexpr const char *kPortalHttpStatusBadRequest = "400 Bad Request";
constexpr const char *kPortalHttpStatusNoContent = "204 No Content";
constexpr const char *kPortalErrorMissingQuery = "缺少请求参数。";
constexpr const char *kPortalWeatherCityInvalidMessage =
    "QWeather 无法识别填写的天气城市，已恢复为自动定位。";
constexpr const char *kPortalWeatherCityDeferredMessage =
    "天气城市已保存，但在线校验超时；下次同步天气时会自动重试。";
constexpr const char *kSetupApSsidFormat = "WeatherClock-%02X%02X";
constexpr const char *kSetupApSsidFallback = "WeatherClock-0000";
constexpr const char *kPortalRootUri = "/";
constexpr const char *kPortalSaveUri = "/save";
constexpr const char *kPortalFaviconUri = "/favicon.ico";
constexpr const char *kPortalAppleTouchIconUri = "/apple-touch-icon.png";
constexpr const char *kPortalAppleTouchIconPrecomposedUri = "/apple-touch-icon-precomposed.png";
constexpr const char *kPortalWildcardUri = "/*";
constexpr const char *kPortalFixedTexts[] = {
    kPortalHttpStatusBadRequest,
    kPortalHttpStatusNoContent,
    kPortalErrorMissingQuery,
    kPortalWeatherCityInvalidMessage,
    kPortalWeatherCityDeferredMessage,
    kSetupApSsidFormat,
    kSetupApSsidFallback,
    kPortalRootUri,
    kPortalSaveUri,
    kPortalFaviconUri,
    kPortalAppleTouchIconUri,
    kPortalAppleTouchIconPrecomposedUri,
    kPortalWildcardUri,
};

struct PortalHttpRoute {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
};

constexpr PortalHttpRoute kPortalHttpRoutes[] = {
    {kPortalRootUri, HTTP_GET, root_get_handler},
    {kPortalSaveUri, HTTP_POST, save_post_handler},
    {kPortalSaveUri, HTTP_GET, save_get_handler},
    {kPortalFaviconUri, HTTP_GET, empty_asset_handler},
    {kPortalAppleTouchIconUri, HTTP_GET, empty_asset_handler},
    {kPortalAppleTouchIconPrecomposedUri, HTTP_GET, empty_asset_handler},
    {kPortalWildcardUri, HTTP_GET, captive_portal_handler},
};
constexpr size_t cstr_len(const char *text)
{
    size_t len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

constexpr bool portal_http_routes_valid()
{
    for (const PortalHttpRoute &route : kPortalHttpRoutes) {
        if (!cstr_nonempty(route.uri) || !route.handler) {
            return false;
        }
    }
    return true;
}

static_assert(kSetupApChannel > 0, "setup AP channel must be positive");
static_assert(kSetupApMaxConnections > 0, "setup AP max connections must be positive");
static_assert(kSetupHttpServerPort > 0, "setup HTTP server port must be positive");
static_assert(kSetupHttpServerStackSize > 0, "setup HTTP server stack must be positive");
static_assert(kPortalRequestBufferSize > kPortalSubmitSsidFieldSize,
              "portal request buffer must exceed submitted SSID field size");
static_assert(kPortalWeatherCityIdSize > 1, "portal weather city id buffer must fit text and NUL");
static_assert(kPortalWeatherCityNameSize > 1, "portal weather city name buffer must fit text and NUL");
static_assert(kPortalSaveWifiConnectWaitMs > 0, "portal save Wi-Fi wait must be positive");
static_assert(cstr_len(kSetupApSsidFallback) < sizeof(g_ap_ssid), "setup AP SSID fallback must fit global buffer");
static_assert(cstr_nonempty(kSetupApSsidFormat), "setup AP SSID format must be non-empty");
static_assert(array_count(kPortalFixedTexts) > 0,
              "portal fixed text registry must not be empty");
static_assert(cstr_array_nonempty(kPortalFixedTexts), "portal fixed texts must be non-empty");
static_assert(array_count(kPortalHttpRoutes) > 0, "portal HTTP route table must not be empty");
static_assert(portal_http_routes_valid(), "portal HTTP routes must have URI and handler");
#define SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG "setup portal running without captive dns"
#define PORTAL_HTTP_SERVER_START_FAILED_FORMAT "http server start failed: %s"
#define PORTAL_HTTP_SERVER_STOP_FAILED_FORMAT "http server stop failed: %s"
#define PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT "http uri register failed: %s"
#define PORTAL_POST_BODY_TRUNCATED_FORMAT "setup POST body truncated content_len=%d buffer=%u"
#define PORTAL_POST_BODY_RECEIVE_FAILED_FORMAT "setup POST body receive failed ret=%d received=%d expected=%d"
#define WIFI_START_SKIPPED_OFFLINE_LOG "wifi start skipped in offline mode"
#define WIFI_STA_ONLY_MODE_FAILED_FORMAT "wifi sta-only mode failed: %s"
#define WIFI_POWER_SAVE_SETUP_FAILED_FORMAT "wifi power save setup failed: %s"
#define WIFI_APSTA_MODE_FAILED_FORMAT "wifi apsta mode failed: %s"
#define WIFI_SOFTAP_CONFIG_FAILED_FORMAT "wifi softap config failed: %s"
#define WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT "wifi setup power save disable failed: %s"
#define WIFI_SETUP_AP_ACTIVE_FORMAT "setup AP active ssid=%s"
#define WIFI_SET_MODE_FAILED_FORMAT "wifi set mode failed: %s"
#define WIFI_START_FAILED_FORMAT "wifi start failed: %s"
#define WIFI_STOP_SKIPPED_OTA_LOG "wifi stop skipped during OTA"
#define WIFI_STOP_SKIPPED_XIAOZHI_LOG "Wi-Fi stop skipped: Xiaozhi AI page is active"
#define WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT "wifi disconnect during stop failed: %s"
#define WIFI_STOP_FAILED_FORMAT "wifi stop failed: %s"
#define WIFI_RADIO_OFF_LOG "wifi radio off"
#define WIFI_STA_CONFIG_FAILED_FORMAT "wifi sta config failed: %s"
#define WIFI_CONNECT_START_FAILED_FORMAT "wifi connect failed to start: %s"
#define MANUAL_WEATHER_CITY_VALIDATED_FORMAT "manual weather city validated: %s id=%s"
#define MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG "manual weather city validation failed, restoring auto location"
#define MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG "manual weather city validation deferred after network/API error"
#define WIFI_DISCONNECTED_FORMAT "wifi disconnected, reason=%d"
#define WIFI_RECONNECT_START_FAILED_FORMAT "wifi reconnect failed to start: %s"
#define WIFI_GOT_IP_EVENT_MISSING_LOG "got ip event missing data"
#define WIFI_GOT_IP_FORMAT "got ip: " IPSTR
#define WIFI_STA_IP_FORMAT_FAILED_LOG "sta ip format failed"
#define WIFI_CONNECTION_EVENT_GROUP_UNAVAILABLE_LOG "wifi connection event unavailable: app events not initialized"
#define WIFI_MAC_READ_FAILED_FORMAT "wifi mac read failed: %s"
#define WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG "setup AP ssid format failed"
#define WIFI_STA_NETIF_CREATE_FAILED_LOG "wifi sta netif create failed"
#define WIFI_AP_NETIF_CREATE_FAILED_LOG "wifi ap netif create failed"
#define WIFI_INIT_FAILED_FORMAT "wifi init failed: %s"
#define WIFI_STORAGE_SETUP_FAILED_FORMAT "wifi storage setup failed: %s"
#define WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT "wifi event handler register failed: %s"
#define WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT "ip event handler register failed: %s"
#define WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT "wifi initial mode setup failed: %s"
#define WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT "wifi initial softap setup failed: %s"
#define PORTAL_PROVISIONING_SYNC_EVENT_UNAVAILABLE_LOG "setup save skipped initial sync request: app events unavailable"
constexpr const char *kPortalLogTexts[] = {
    SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG,
    PORTAL_HTTP_SERVER_START_FAILED_FORMAT,
    PORTAL_HTTP_SERVER_STOP_FAILED_FORMAT,
    PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT,
    PORTAL_POST_BODY_TRUNCATED_FORMAT,
    PORTAL_POST_BODY_RECEIVE_FAILED_FORMAT,
    WIFI_START_SKIPPED_OFFLINE_LOG,
    WIFI_STA_ONLY_MODE_FAILED_FORMAT,
    WIFI_POWER_SAVE_SETUP_FAILED_FORMAT,
    WIFI_APSTA_MODE_FAILED_FORMAT,
    WIFI_SOFTAP_CONFIG_FAILED_FORMAT,
    WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT,
    WIFI_SETUP_AP_ACTIVE_FORMAT,
    WIFI_SET_MODE_FAILED_FORMAT,
    WIFI_START_FAILED_FORMAT,
    WIFI_STOP_SKIPPED_OTA_LOG,
    WIFI_STOP_SKIPPED_XIAOZHI_LOG,
    WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT,
    WIFI_STOP_FAILED_FORMAT,
    WIFI_RADIO_OFF_LOG,
    WIFI_STA_CONFIG_FAILED_FORMAT,
    WIFI_CONNECT_START_FAILED_FORMAT,
    MANUAL_WEATHER_CITY_VALIDATED_FORMAT,
    MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG,
    MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG,
    WIFI_DISCONNECTED_FORMAT,
    WIFI_RECONNECT_START_FAILED_FORMAT,
    WIFI_GOT_IP_EVENT_MISSING_LOG,
    WIFI_GOT_IP_FORMAT,
    WIFI_STA_IP_FORMAT_FAILED_LOG,
    WIFI_CONNECTION_EVENT_GROUP_UNAVAILABLE_LOG,
    WIFI_MAC_READ_FAILED_FORMAT,
    WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG,
    WIFI_STA_NETIF_CREATE_FAILED_LOG,
    WIFI_AP_NETIF_CREATE_FAILED_LOG,
    WIFI_INIT_FAILED_FORMAT,
    WIFI_STORAGE_SETUP_FAILED_FORMAT,
    WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT,
    WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT,
    WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT,
    WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT,
    PORTAL_PROVISIONING_SYNC_EVENT_UNAVAILABLE_LOG,
};
static_assert(array_count(kPortalLogTexts) > 0, "portal log text registry must not be empty");
static_assert(cstr_array_nonempty(kPortalLogTexts), "portal log texts must be non-empty");

void request_provisioning_sync_after_save()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", PORTAL_PROVISIONING_SYNC_EVENT_UNAVAILABLE_LOG);
        return;
    }
    xEventGroupSetBits(g_app_events, kProvisioningSyncBit);
}
void format_sta_ip_or_clear(const esp_ip4_addr_t *ip)
{
    if (!ip) {
        g_sta_ip[0] = '\0';
        return;
    }
    int written = snprintf(g_sta_ip, sizeof(g_sta_ip), IPSTR, IP2STR(ip));
    if (app_text::format_failed(written, sizeof(g_sta_ip))) {
        g_sta_ip[0] = '\0';
        ESP_LOGW(TAG, WIFI_STA_IP_FORMAT_FAILED_LOG);
    }
}

void set_wifi_connected_event(bool connected)
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", WIFI_CONNECTION_EVENT_GROUP_UNAVAILABLE_LOG);
        return;
    }
    if (connected) {
        xEventGroupSetBits(g_app_events, kWifiConnectedBit);
    } else {
        xEventGroupClearBits(g_app_events, kWifiConnectedBit);
    }
}

void clear_sta_connection_state()
{
    g_sta_ip[0] = '\0';
    set_wifi_connected_event(false);
}

void format_setup_ap_ssid(uint8_t mac4, uint8_t mac5)
{
    int written = snprintf(g_ap_ssid, sizeof(g_ap_ssid), kSetupApSsidFormat, mac4, mac5);
    if (app_text::format_failed(written, sizeof(g_ap_ssid))) {
        strlcpy(g_ap_ssid, kSetupApSsidFallback, sizeof(g_ap_ssid));
        ESP_LOGW(TAG, WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG);
    }
}

esp_err_t configure_softap()
{
    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, g_ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, kSetupApPassword, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(g_ap_ssid);
    ap_config.ap.channel = kSetupApChannel;
    ap_config.ap.max_connection = kSetupApMaxConnections;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

} // namespace

bool apply_station_config(bool reconnect)
{
    wifi_config_t sta_config = {};
    strlcpy((char *)sta_config.sta.ssid, g_wifi_ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, g_wifi_pass, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = g_wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_STA_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    if (reconnect) {
        esp_wifi_disconnect();
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, WIFI_CONNECT_START_FAILED_FORMAT, esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

static bool stop_http_server_handle()
{
    if (!g_http_server) {
        return true;
    }
    esp_err_t err = httpd_stop(g_http_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, PORTAL_HTTP_SERVER_STOP_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    g_http_server = nullptr;
    return true;
}

void stop_http_server()
{
    (void)stop_http_server_handle();
    stop_captive_dns_server();
    g_setup_portal_active = false;
}

enum ManualWeatherCityValidationResult {
    kManualWeatherCityValidationOk,
    kManualWeatherCityValidationInvalid,
    kManualWeatherCityValidationDeferred,
};

static ManualWeatherCityValidationResult validate_saved_manual_weather_city()
{
    if (!g_has_manual_weather_city || g_manual_weather_city[0] == '\0') {
        return kManualWeatherCityValidationOk;
    }
    char city_id[kPortalWeatherCityIdSize] = {};
    char city_name[kPortalWeatherCityNameSize] = {};
    QweatherCityLookupStatus status = qweather_lookup_city_status(g_manual_weather_city,
                                                                  city_id,
                                                                  sizeof(city_id),
                                                                  city_name,
                                                                  sizeof(city_name));
    if (status == kQweatherCityLookupOk) {
        ESP_LOGI(TAG, MANUAL_WEATHER_CITY_VALIDATED_FORMAT, city_name, city_id);
        return kManualWeatherCityValidationOk;
    }
    if (status == kQweatherCityLookupNotFound) {
        ESP_LOGW(TAG, MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG);
        (void)clear_manual_weather_city();
        return kManualWeatherCityValidationInvalid;
    }
    ESP_LOGW(TAG, MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG);
    return kManualWeatherCityValidationDeferred;
}

static esp_err_t handle_setup_save(httpd_req_t *req, const char *body)
{
    char ssid[kPortalSubmitSsidFieldSize] = {};
    form_value(body, "ssid", ssid, sizeof(ssid));
    trim_ascii(ssid);
    if (ssid[0] == '\0') {
        bool offline_saved = save_offline_datetime_from_body(body);
        esp_err_t err = send_offline_result_page(req, offline_saved);
        if (offline_saved) {
            g_settings_requested = false;
            g_network_diag_page_requested = false;
            g_boot_info_requested = false;
            stop_wifi_radio(true);
            notify_ui_task();
        }
        return err;
    }
    bool saved = save_credentials_from_body(body);
    bool connected = saved && wait_for_wifi_connected(kPortalSaveWifiConnectWaitMs);
    const char *extra_message = nullptr;
    if (connected && g_has_manual_weather_city) {
        ManualWeatherCityValidationResult city_result = validate_saved_manual_weather_city();
        if (city_result == kManualWeatherCityValidationInvalid) {
            extra_message = kPortalWeatherCityInvalidMessage;
        } else if (city_result == kManualWeatherCityValidationDeferred) {
            extra_message = kPortalWeatherCityDeferredMessage;
        }
    }
    esp_err_t err = send_save_result_page(req, saved, connected, extra_message);
    if (connected) {
        request_provisioning_sync_after_save();
    }
    return err;
}

esp_err_t receive_portal_post_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (!req || !body || body_size < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    int total = 0;
    const int capacity = (int)body_size - 1;
    while (total < req->content_len && total < capacity) {
        int ret = httpd_req_recv(req, body + total, capacity - total);
        if (ret <= 0) {
            ESP_LOGW(TAG, PORTAL_POST_BODY_RECEIVE_FAILED_FORMAT, ret, total, req->content_len);
            return ESP_FAIL;
        }
        total += ret;
    }
    body[total] = '\0';
    if (total < req->content_len) {
        ESP_LOGW(TAG, PORTAL_POST_BODY_TRUNCATED_FORMAT, req->content_len, (unsigned)body_size);
    }
    return ESP_OK;
}

esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[kPortalRequestBufferSize] = {};
    esp_err_t err = receive_portal_post_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return err;
    }
    return handle_setup_save(req, body);
}

esp_err_t save_get_handler(httpd_req_t *req)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    char query[kPortalRequestBufferSize] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_portal_text_status(req, kPortalHttpStatusBadRequest, kPortalErrorMissingQuery);
    }
    return handle_setup_save(req, query);
}

esp_err_t empty_asset_handler(httpd_req_t *req)
{
    return send_portal_empty_status(req, kPortalHttpStatusNoContent);
}

esp_err_t captive_portal_handler(httpd_req_t *req)
{
    return redirect_to_setup_portal(req);
}

esp_err_t register_http_handler(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    return httpd_register_uri_handler(server, &route);
}

bool start_http_server()
{
    if (g_http_server && !g_setup_portal_active && !stop_http_server_handle()) {
        return false;
    }
    if (g_http_server) {
        g_setup_portal_active = true;
        if (!start_captive_dns_server()) {
            ESP_LOGW(TAG, SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG);
        }
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kSetupHttpServerPort;
    config.stack_size = kSetupHttpServerStackSize;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&g_http_server, &config);
    if (err != ESP_OK) {
        g_http_server = nullptr;
        g_setup_portal_active = false;
        ESP_LOGW(TAG, PORTAL_HTTP_SERVER_START_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }

    for (const PortalHttpRoute &route : kPortalHttpRoutes) {
        if (err != ESP_OK) {
            break;
        }
        err = register_http_handler(g_http_server, route.uri, route.method, route.handler);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        (void)stop_http_server_handle();
        g_setup_portal_active = false;
        return false;
    }
    if (!start_captive_dns_server()) {
        ESP_LOGW(TAG, SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG);
    }
    g_setup_portal_active = true;
    return true;
}

bool start_wifi_radio(bool enable_setup_portal)
{
    if (g_offline_mode_ui_enabled && !enable_setup_portal) {
        ESP_LOGI(TAG, WIFI_START_SKIPPED_OFFLINE_LOG);
        return false;
    }
    bool entering_setup_portal = enable_setup_portal && !g_setup_portal_active;
    if (g_wifi_radio_on) {
        if (!enable_setup_portal) {
            stop_http_server();
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_STA_ONLY_MODE_FAILED_FORMAT, esp_err_to_name(mode_err));
                return false;
            }
            esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
            if (ps_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_POWER_SAVE_SETUP_FAILED_FORMAT, esp_err_to_name(ps_err));
            }
        } else {
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_APSTA_MODE_FAILED_FORMAT, esp_err_to_name(mode_err));
                return false;
            }
            esp_err_t ap_err = configure_softap();
            if (ap_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_SOFTAP_CONFIG_FAILED_FORMAT, esp_err_to_name(ap_err));
                return false;
            }
            esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT, esp_err_to_name(ps_err));
            }
            if (!g_have_wifi_creds) {
                (void)esp_wifi_disconnect();
                clear_sta_connection_state();
            }
        }
        if (enable_setup_portal && !g_setup_portal_active) {
            if (!start_http_server()) {
                return false;
            }
            ESP_LOGI(TAG, WIFI_SETUP_AP_ACTIVE_FORMAT, g_ap_ssid);
        }
        if (g_have_wifi_creds) {
            (void)apply_station_config(true);
        }
        if (entering_setup_portal) {
            request_setup_prompt_once();
        }
        return true;
    }

    g_wifi_stop_requested = false;
    esp_err_t err = esp_wifi_set_mode(enable_setup_portal ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_SET_MODE_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    if (enable_setup_portal) {
        err = configure_softap();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, WIFI_SOFTAP_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
            return false;
        }
    }
    if (g_have_wifi_creds) {
        (void)apply_station_config(false);
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_START_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    esp_err_t ps_err = esp_wifi_set_ps(enable_setup_portal ? WIFI_PS_NONE : WIFI_PS_MAX_MODEM);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_POWER_SAVE_SETUP_FAILED_FORMAT, esp_err_to_name(ps_err));
    }
    if (enable_setup_portal) {
        if (!start_http_server()) {
            g_wifi_stop_requested = true;
            (void)esp_wifi_disconnect();
            (void)esp_wifi_stop();
            return false;
        }
        if (entering_setup_portal) {
            request_setup_prompt_once();
        }
        ESP_LOGI(TAG, WIFI_SETUP_AP_ACTIVE_FORMAT, g_ap_ssid);
    }
    g_wifi_radio_on = true;
    return true;
}

void stop_wifi_radio(bool force_setup_portal)
{
    if (!g_wifi_radio_on) {
        return;
    }
    if ((g_ota_state == kOtaChecking || g_ota_state == kOtaUpdating) && !force_setup_portal) {
        ESP_LOGI(TAG, WIFI_STOP_SKIPPED_OTA_LOG);
        return;
    }
    if (xiaozhi_ai_network_keepalive_active() && !force_setup_portal) {
        ESP_LOGI(TAG, WIFI_STOP_SKIPPED_XIAOZHI_LOG);
        return;
    }
    if (g_setup_portal_active && !force_setup_portal) {
        return;
    }
    if (!g_have_wifi_creds && !force_setup_portal) {
        return;
    }
    stop_http_server();
    g_wifi_stop_requested = true;
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT, esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, WIFI_STOP_FAILED_FORMAT, esp_err_to_name(err));
        g_wifi_stop_requested = false;
    } else {
        g_wifi_radio_on = false;
        g_wifi_stop_requested = false;
        clear_sta_connection_state();
        ESP_LOGI(TAG, WIFI_RADIO_OFF_LOG);
    }
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && g_have_wifi_creds) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, WIFI_CONNECT_START_FAILED_FORMAT, esp_err_to_name(err));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        g_last_wifi_disconnect_reason = event ? event->reason : -1;
        clear_sta_connection_state();
        ESP_LOGW(TAG, WIFI_DISCONNECTED_FORMAT, event ? event->reason : -1);
        notify_ui_task();
        if (g_have_wifi_creds && g_wifi_radio_on && !g_wifi_stop_requested) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, WIFI_RECONNECT_START_FAILED_FORMAT, esp_err_to_name(err));
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (!event) {
            ESP_LOGW(TAG, WIFI_GOT_IP_EVENT_MISSING_LOG);
            return;
        }
        ESP_LOGI(TAG, WIFI_GOT_IP_FORMAT, IP2STR(&event->ip_info.ip));
        format_sta_ip_or_clear(&event->ip_info.ip);
        set_wifi_connected_event(true);
        notify_ui_task();
    }
}

void init_wifi()
{
    uint8_t mac[6] = {};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_MAC_READ_FAILED_FORMAT, esp_err_to_name(err));
    }
    format_setup_ap_ssid(mac[4], mac[5]);

    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGW(TAG, WIFI_STA_NETIF_CREATE_FAILED_LOG);
        return;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGW(TAG, WIFI_AP_NETIF_CREATE_FAILED_LOG);
        return;
    }
    configure_captive_portal_dhcp(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INIT_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_STORAGE_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = configure_softap();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }

    if (!g_have_wifi_creds && !g_offline_mode_ui_enabled) {
        start_wifi_radio(true);
    }
}
