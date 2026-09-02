// 声明联网凭据、离线模式、天气城市和恢复出厂配置入口。
#pragma once

#include "wifi_failover_policy.h"

#include <stddef.h>

bool save_config(const char *ssid,
                 const char *pass,
                 const char *backup_ssid,
                 const char *backup_pass,
                 const char *ntp_server,
                 const char *weather_city = nullptr);
bool set_offline_mode_enabled(bool enabled);
bool can_leave_offline_mode_without_setup();
bool persist_preferred_wifi_slot(WifiCredentialSlot slot);
bool save_manual_weather_city(const char *city);
bool clear_manual_weather_city();
bool is_weather_city_input_valid(const char *city);
bool normalize_weather_city_input(const char *city, char *out, size_t out_len);
bool clear_saved_config();
