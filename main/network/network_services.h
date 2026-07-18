// 声明配网、Wi-Fi、HTTP、天气、NTP、OTA 调度等网络服务接口。
#pragma once

#include "weather_types.h"

#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_http_client.h"
#include "esp_http_server.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct HttpBuffer {
    char *data;
    size_t len;
    size_t cap;
};

bool load_saved_config();
bool save_config(const char *ssid, const char *pass, const char *api_key, const char *weather_city = nullptr);
bool save_manual_weather_city(const char *city);
bool clear_manual_weather_city();
bool is_weather_city_input_valid(const char *city);
bool normalize_weather_city_input(const char *city, char *out, size_t out_len);
bool set_offline_mode_enabled(bool enabled);
bool can_leave_offline_mode_without_setup();
void clear_network_request_bits();
bool save_hourly_chime_setting();
bool save_work_page_settings();
bool save_work_page_order();
bool save_xiaozhi_auto_return_setting();
bool clear_saved_config();
bool save_credentials_from_body(const char *body);
bool save_offline_datetime_from_body(const char *body);
bool apply_station_config(bool reconnect);
void stop_http_server();
esp_err_t save_post_handler(httpd_req_t *req);
esp_err_t save_get_handler(httpd_req_t *req);
esp_err_t portal_status_get_handler(httpd_req_t *req);
esp_err_t empty_asset_handler(httpd_req_t *req);
esp_err_t captive_portal_handler(httpd_req_t *req);
bool start_captive_dns_server();
void stop_captive_dns_server();
bool start_http_server();
bool request_setup_portal_start();
bool setup_portal_start_requested();
bool prepare_setup_portal_result_delivery();
bool start_wifi_radio(bool enable_setup_portal);
void stop_wifi_radio(bool force_setup_portal = false);
// 小智退出与后台同步可能交错：先登记关闭请求，再由最后释放联网锁的
// 任务补关 Wi-Fi，避免双方都因对方仍持锁而永久漏关射频。
void request_wifi_radio_stop_when_idle();
void service_wifi_radio_stop_when_idle();
void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data);
void init_wifi();
int boot_sync_remaining_ms();
esp_err_t http_event_handler(esp_http_client_event_t *evt);
esp_err_t decode_http_body(char *out, size_t out_len, size_t *body_len);
esp_err_t http_get_text(const char *url, char *out, size_t out_len, const char *api_key = nullptr);
void log_response_preview(const char *stage, const char *response);
bool ip_geolocation_lookup(char *location, size_t location_len, char *city, size_t city_len);
enum QweatherCityLookupStatus {
    kQweatherCityLookupOk = 0,
    kQweatherCityLookupNotFound = 1,
    kQweatherCityLookupError = 2,
};
QweatherCityLookupStatus qweather_lookup_city_status(const char *location,
                                                      char *city_id,
                                                      size_t city_id_len,
                                                      char *city_name,
                                                      size_t city_name_len,
                                                      char *lat_out = nullptr,
                                                      size_t lat_len = 0,
                                                      char *lon_out = nullptr,
                                                      size_t lon_len = 0);
bool qweather_lookup_city(const char *location,
                          char *city_id,
                          size_t city_id_len,
                          char *city_name,
                          size_t city_name_len,
                          char *lat_out = nullptr,
                          size_t lat_len = 0,
                          char *lon_out = nullptr,
                          size_t lon_len = 0);
bool qweather_fetch_alert(const char *lat, const char *lon, WeatherAlertData *alert);
bool qweather_fetch_now(const char *city_id, WeatherData *weather);
bool qweather_fetch_daily(const char *city_id, WeatherForecastData *forecast);
bool qweather_fetch_air(const char *city_id, WeatherAirData *air);
enum class WeatherUpdateResult {
    kSuccess,
    kFailed,
    kResourceDeferred,
};
WeatherUpdateResult perform_weather_update();
bool perform_daily_saying_update();
bool wait_for_wifi_connected(uint32_t timeout_ms);
bool is_time_valid(struct tm *local_out = nullptr);
void run_boot_connectivity_sync();
void boot_connectivity_task(void *);
uint32_t network_idle_wait_ms(time_t now,
                              time_t next_boot_due_at,
                              time_t next_ntp_retry_at,
                              time_t next_daily_ntp_at);
void network_diag_reset();
void network_diag_begin();
void network_diag_finish();
void network_diag_set_line(int index, const char *fmt, ...);
// 只执行检测项目；调用方负责 begin/finish 与 Wi-Fi 会话生命周期。
void run_network_diagnostic_checks();
void network_sync_task(void *);
