// 实现配网 HTTP 路由、表单保存和强制门户服务生命周期。
#include "network_services.h"

#include "app_constexpr.h"
#include "app_event_group.h"
#include "network_diagnostics_state.h"
#include "network_form.h"
#include "network_text.h"
#include "wifi_portal_dns.h"
#include "wifi_portal_pages.h"
#include "wifi_portal_state.h"

#include "ui_info_page_state.h"
#include "ui_settings_activity_state.h"
#include "ui_views.h"

#include "display_bsp.h"

namespace {
httpd_handle_t s_http_server = nullptr;
bool s_display_dma_guard_active = false;
constexpr uint16_t kSetupHttpServerPort = 80;
constexpr size_t kSetupHttpServerStackSize = 8192;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalRequestBufferSize = 640;
constexpr uint32_t kPortalResponseSettleMs = 750;
constexpr const char *kPortalHttpStatusBadRequest = "400 Bad Request";
constexpr const char *kPortalHttpStatusOk = "200 OK";
constexpr const char *kPortalHttpStatusConflict = "409 Conflict";
constexpr const char *kPortalHttpStatusNoContent = "204 No Content";
constexpr const char *kPortalErrorMissingQuery = "缺少请求参数。";
constexpr const char *kPortalRootUri = "/";
constexpr const char *kPortalSaveUri = "/save";
constexpr const char *kPortalStatusUri = "/status";
constexpr const char *kPortalFaviconUri = "/favicon.ico";
constexpr const char *kPortalAppleTouchIconUri = "/apple-touch-icon.png";
constexpr const char *kPortalAppleTouchIconPrecomposedUri = "/apple-touch-icon-precomposed.png";
constexpr const char *kPortalWildcardUri = "/*";
struct PortalHttpRoute {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
};

constexpr PortalHttpRoute kPortalHttpRoutes[] = {
    {kPortalRootUri, HTTP_GET, root_get_handler},
    {kPortalSaveUri, HTTP_POST, save_post_handler},
    {kPortalSaveUri, HTTP_GET, save_get_handler},
    {kPortalStatusUri, HTTP_GET, portal_status_get_handler},
    {kPortalFaviconUri, HTTP_GET, empty_asset_handler},
    {kPortalAppleTouchIconUri, HTTP_GET, empty_asset_handler},
    {kPortalAppleTouchIconPrecomposedUri, HTTP_GET, empty_asset_handler},
    {kPortalWildcardUri, HTTP_GET, captive_portal_handler},
};

constexpr bool portal_http_routes_valid()
{
    for (const PortalHttpRoute &route : kPortalHttpRoutes) {
        if (!cstr_nonempty(route.uri) || !route.handler) {
            return false;
        }
    }
    return true;
}

static_assert(kSetupHttpServerPort > 0, "setup HTTP server port must be positive");
static_assert(kSetupHttpServerStackSize > 0, "setup HTTP server stack must be positive");
static_assert(kPortalRequestBufferSize > kPortalSubmitSsidFieldSize,
              "portal request buffer must exceed submitted SSID field size");
static_assert(kPortalResponseSettleMs > 0,
              "portal response settle delay must be positive");
static_assert(array_count(kPortalHttpRoutes) > 0, "portal HTTP route table must not be empty");
static_assert(portal_http_routes_valid(), "portal HTTP routes must have URI and handler");

#define SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG "setup portal running without captive dns"
#define PORTAL_HTTP_SERVER_START_FAILED_FORMAT "http server start failed: %s"
#define PORTAL_HTTP_SERVER_STOP_FAILED_FORMAT "http server stop failed: %s"
#define PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT "http uri register failed: %s"
#define PORTAL_POST_BODY_TRUNCATED_FORMAT "setup POST body truncated content_len=%d buffer=%u"
#define PORTAL_POST_BODY_RECEIVE_FAILED_FORMAT "setup POST body receive failed ret=%d received=%d expected=%d"
#define PORTAL_PROVISIONING_SYNC_EVENT_UNAVAILABLE_LOG "setup save skipped initial sync request: app events unavailable"

bool request_provisioning_sync_after_save()
{
    if (!app_event_group_ready()) {
        ESP_LOGW(TAG, "%s", PORTAL_PROVISIONING_SYNC_EVENT_UNAVAILABLE_LOG);
        return false;
    }
    app_event_group_set_bits(kProvisioningSyncBit);
    return true;
}

void acquire_portal_display_dma_guard()
{
    if (s_display_dma_guard_active) {
        return;
    }
    Display_AcquireDmaConservativeMode();
    s_display_dma_guard_active = true;
}

void release_portal_display_dma_guard()
{
    if (!s_display_dma_guard_active) {
        return;
    }
    Display_ReleaseDmaConservativeMode();
    s_display_dma_guard_active = false;
}

bool stop_http_server_handle()
{
    if (!s_http_server) {
        release_portal_display_dma_guard();
        return true;
    }
    esp_err_t err = httpd_stop(s_http_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, PORTAL_HTTP_SERVER_STOP_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    s_http_server = nullptr;
    release_portal_display_dma_guard();
    return true;
}

esp_err_t handle_setup_save(httpd_req_t *req, const char *body)
{
    wifi_portal_save_result_store(WifiPortalSaveResult::kNone);
    wifi_portal_save_feedback_seen_store(false);
    char ssid[kPortalSubmitSsidFieldSize] = {};
    form_value(body, "ssid", ssid, sizeof(ssid));
    trim_ascii(ssid);
    if (ssid[0] == '\0') {
        bool offline_saved = save_offline_datetime_from_body(body);
        if (!offline_saved) {
            wifi_portal_save_result_store(WifiPortalSaveResult::kInvalidInput);
        }
        esp_err_t err = send_offline_result_page(req, offline_saved);
        if (offline_saved) {
            settings_page_clear();
            network_diag_page_clear();
            info_page_clear();
            stop_wifi_radio(true);
            notify_ui_task();
        }
        return err;
    }
    const bool saved = save_credentials_from_body(body);
    WifiPortalSaveResult result = saved
                                      ? WifiPortalSaveResult::kValidating
                                      : WifiPortalSaveResult::kInvalidInput;
    wifi_portal_save_result_store(result);
    esp_err_t err = send_save_result_page(req, result);
    if (saved) {
        // httpd_resp_send() has handed the body to lwIP, but changing an APSTA
        // channel immediately afterwards can still disconnect the phone before
        // its captive browser renders the result page.
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(kPortalResponseSettleMs));
        }
        if (!request_provisioning_sync_after_save()) {
            wifi_portal_save_result_store(WifiPortalSaveResult::kWifiConnectionFailed);
        }
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

esp_err_t register_http_handler(httpd_handle_t server,
                                const char *uri,
                                httpd_method_t method,
                                esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    return httpd_register_uri_handler(server, &route);
}
} // namespace

void stop_http_server()
{
    (void)stop_http_server_handle();
    stop_captive_dns_server();
    setup_portal_active_store(false);
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

esp_err_t portal_status_get_handler(httpd_req_t *req)
{
    const WifiPortalSaveResult result = wifi_portal_save_result_load();
    if (result == WifiPortalSaveResult::kSuccess) {
        esp_err_t err = send_portal_empty_status(req, kPortalHttpStatusOk);
        if (err == ESP_OK) {
            wifi_portal_save_feedback_seen_store(true);
        }
        return err;
    }
    if (result != WifiPortalSaveResult::kNone &&
        result != WifiPortalSaveResult::kValidating) {
        return send_portal_empty_status(req, kPortalHttpStatusConflict);
    }
    return send_portal_empty_status(req, kPortalHttpStatusNoContent);
}

esp_err_t empty_asset_handler(httpd_req_t *req)
{
    return send_portal_empty_status(req, kPortalHttpStatusNoContent);
}

esp_err_t captive_portal_handler(httpd_req_t *req)
{
    return redirect_to_setup_portal(req);
}

bool start_http_server()
{
    if (s_http_server && !setup_portal_active_load() && !stop_http_server_handle()) {
        return false;
    }
    if (s_http_server) {
        acquire_portal_display_dma_guard();
        setup_portal_active_store(true);
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
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) {
        s_http_server = nullptr;
        setup_portal_active_store(false);
        ESP_LOGW(TAG, PORTAL_HTTP_SERVER_START_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }

    for (const PortalHttpRoute &route : kPortalHttpRoutes) {
        if (err != ESP_OK) {
            break;
        }
        err = register_http_handler(s_http_server, route.uri, route.method, route.handler);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        (void)stop_http_server_handle();
        setup_portal_active_store(false);
        return false;
    }
    if (!start_captive_dns_server()) {
        ESP_LOGW(TAG, SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG);
    }
    acquire_portal_display_dma_guard();
    setup_portal_active_store(true);
    return true;
}
