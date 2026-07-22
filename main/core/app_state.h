// 声明天气时钟全局状态、常量、数据结构和跨模块共享对象。
#pragma once
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "miniz.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"

#include "active_work_page_state.h"
#include "app_metadata.h"
#include "app_network_config.h"
#include "app_time_constants.h"
#include "app_event_group.h"
#include "work_page_ids.h"
#include "battery_policy.h"
#include "battery_runtime_state.h"
#include "lvgl_bsp.h"
#include "dseg_digits.h"
#include "network_diagnostics_catalog.h"
#include "daily_saying_contract.h"
#include "weather_city_contract.h"
#include "weather_types.h"
#include "ota_download_policy.h"
#include "ota_flow_policy.h"
#include "ota_manifest_limits.h"
#include "sensor_history_types.h"
#include "status_gif_60.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_settings_contract.h"

inline constexpr int kDisplayWidth = 400;
inline constexpr int kDisplayHeight = 300;
inline constexpr int kAppMsPerSecond = 1000;
inline constexpr int kAppSecondsPerMinute = 60;
inline constexpr int kAppMsPerMinute = kAppSecondsPerMinute * kAppMsPerSecond;
inline constexpr int kSettingsTimeoutMs = 30 * kAppMsPerSecond;
inline constexpr int kXiaozhiAutoReturnTimeoutMs = 5 * kAppMsPerMinute;
inline constexpr int kSettingsManualSyncTimeoutMs = kAppMsPerMinute;
inline constexpr int kWeatherClockAutoRetryMs = 2 * kAppMsPerMinute;
inline constexpr int kWeatherClockAutoSyncMaxAttempts = 3;
inline constexpr int kWeatherClockAutoBackoffMs = 30 * kAppMsPerMinute;
inline constexpr int kBootAnimRunFrameMs = 50;
inline constexpr int kHttpBootTimeoutMs = 2500;
inline constexpr int kDisplayPartialMaxWidth = (kDisplayWidth * 7) / 10;
inline constexpr int kMaxFlushRanges = 8;
inline constexpr int kFlushRangeMergeGap = 8;
inline constexpr int kDisplayFlushDiagIntervalMs = kAppMsPerMinute;
static_assert(kXiaozhiAutoReturnTimeoutMs > 0, "Xiaozhi auto-return timeout must be positive");
