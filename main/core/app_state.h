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
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"

#include "active_work_page_state.h"
#include "app_event_group.h"
#include "work_page_ids.h"
#include "battery_runtime_state.h"
#include "lvgl_bsp.h"
#include "dseg_digits.h"
#include "network_diagnostics_catalog.h"
#include "weather_city_contract.h"
#include "weather_types.h"
#include "ota_flow_policy.h"
#include "ota_manifest_limits.h"
#include "sensor_history_types.h"
#include "status_gif_60.h"
#include "ui_icons.h"
#include "ui_settings_contract.h"

LV_FONT_DECLARE(qweather_icons_36);
LV_FONT_DECLARE(zh_font_16);
LV_FONT_DECLARE(zh_flip_lunar_22);
LV_FONT_DECLARE(zh_pomodoro_title_24);

extern const char *const TAG;
extern const char *const APP_VERSION;
extern const char *const APP_BUILD_DATE;

inline constexpr int kDisplayWidth = 400;
inline constexpr int kDisplayHeight = 300;
inline constexpr gpio_num_t kBootButtonGpio = GPIO_NUM_0;
inline constexpr gpio_num_t kKeyButtonGpio = GPIO_NUM_18;
inline constexpr const char *kSetupApPassword = "12345678";
inline constexpr const char *kSetupPortalIp = "192.168.4.1";
inline constexpr const char *kSetupPortalUrl = "http://192.168.4.1/";
inline constexpr int kAppMsPerSecond = 1000;
inline constexpr int kAppSecondsPerMinute = 60;
inline constexpr int kAppMsPerMinute = kAppSecondsPerMinute * kAppMsPerSecond;
inline constexpr int kSettingsTimeoutMs = 30 * kAppMsPerSecond;
inline constexpr int kXiaozhiAutoReturnTimeoutMs = 5 * kAppMsPerMinute;
inline constexpr int kChimeSoundCount = 4;
inline constexpr int kSettingsManualSyncTimeoutMs = kAppMsPerMinute;
inline constexpr int kWeatherClockAutoRetryMs = 2 * kAppMsPerMinute;
inline constexpr int kWeatherClockAutoSyncMaxAttempts = 3;
inline constexpr int kWeatherClockAutoBackoffMs = 30 * kAppMsPerMinute;
inline constexpr int kButtonIdlePollMs = 60;
inline constexpr int kButtonLowRefreshIdlePollMs = 50;
inline constexpr int kButtonActivePollMs = 50;
inline constexpr int kButtonPressedPollMs = 20;
inline constexpr int kBootAnimRunFrameMs = 50;
inline constexpr int kBootWifiConnectTimeoutMs = 5 * kAppMsPerSecond;
inline constexpr int kBootNtpRetries = 2;
inline constexpr int kBootStartupBudgetMs = 6 * kAppMsPerSecond;
inline constexpr int kHttpBootTimeoutMs = 2500;
inline constexpr int kMinValidYear = 2024;
inline constexpr int kMaxValidYear = 2035;
inline constexpr int kLowBatteryEnterPercent = 10;
inline constexpr int kLowBatteryExitPercent = 13;
inline constexpr int kDisplayPartialMaxWidth = (kDisplayWidth * 7) / 10;
inline constexpr int kMaxFlushRanges = 8;
inline constexpr int kFlushRangeMergeGap = 8;
inline constexpr int kDisplayFlushDiagIntervalMs = kAppMsPerMinute;
inline constexpr int kDailySayingLen = 160;
inline constexpr const char *kDailySayingUrl = "https://uapis.cn/api/v1/saying";
#ifndef WEATHER_CLOCK_OTA_MANIFEST_URL
#if __has_include("ota_endpoint_local.h")
#include "ota_endpoint_local.h"
#else
#define WEATHER_CLOCK_OTA_MANIFEST_URL "https://example.invalid/firmware/latest.json"
#endif
#endif
inline constexpr const char *kOtaManifestUrl = WEATHER_CLOCK_OTA_MANIFEST_URL;
#ifndef WEATHER_CLOCK_OTA_BACKUP_MANIFEST_URL
#define WEATHER_CLOCK_OTA_BACKUP_MANIFEST_URL "https://example.invalid/firmware/latest.json"
#endif
inline constexpr const char *kOtaBackupManifestUrl = WEATHER_CLOCK_OTA_BACKUP_MANIFEST_URL;
inline constexpr int kOtaHttpTimeoutMs = 8 * kAppMsPerSecond;
inline constexpr int kOtaNoProgressTimeoutMs = 45 * kAppMsPerSecond;
inline constexpr int kOtaMaxDownloadMs = 10 * kAppMsPerMinute;
inline constexpr int kOtaStatusMinIntervalMs = 3 * kAppMsPerSecond;
inline constexpr int kOtaAvailableConfirmTimeoutMs = kAppMsPerMinute;
inline constexpr int kOtaDownloadBufferSize = 4096;
inline constexpr int kOtaChunkDelayMs = 25;
inline constexpr float kBatteryChargingRiseVoltage = 0.035f;
inline constexpr float kBatteryChargingStopVoltage = 0.006f;
inline constexpr int kBatteryChargingRiseSamples = 1;
inline constexpr int kBatteryChargingStopSamples = 5;
inline constexpr int kBatteryChargingAnimationStopPercent = 96;
inline constexpr int kBatteryChargingAnimationIdleMs = 10 * kAppMsPerMinute;
inline constexpr int kBatteryChargingSampleMs = kAppMsPerSecond;

static_assert(kXiaozhiAutoReturnTimeoutMs > 0, "Xiaozhi auto-return timeout must be positive");
