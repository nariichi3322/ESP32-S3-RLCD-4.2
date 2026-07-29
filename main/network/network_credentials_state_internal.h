// 限定配置保存与启动加载模块发布联网凭据，普通消费者仅使用只读快照。
#pragma once

#include "network_credentials_state.h"

void network_credentials_store(const char *ssid,
                               const char *password,
                               const char *weather_api_key,
                               const char *weather_api_host,
                               bool wifi_configured,
                               bool weather_api_key_configured,
                               bool weather_api_host_configured);
void network_credentials_clear();
