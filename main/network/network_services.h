// 声明配网、Wi-Fi、天气与后台同步调度等聚合网络服务接口。
#pragma once

#include <stddef.h>

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
void network_sync_task(void *);
