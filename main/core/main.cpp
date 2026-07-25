// 初始化硬件、系统服务和常驻任务，是固件应用入口。
#include "app_display_config.h"
#include "app_event_group.h"
#include "app_hardware.h"
#include "app_constexpr.h"
#include "app_metadata.h"
#include "lvgl_bsp.h"
#include "alarm_services.h"
#include "battery_runtime_state.h"
#include "pomodoro_services.h"
#include "weather_city_mcp.h"
#include "audio_services.h"
#include "custom_assets.h"
#include "daily_saying_state.h"
#include "input_tasks.h"
#include "manual_weather_city_state.h"
#include "network_credentials_state.h"
#include "network_diagnostics_state.h"
#include "network_boot_sync.h"
#include "network_http_transaction_lock.h"
#include "network_sync_task.h"
#include "saved_config_loader.h"
#include "wifi_radio_services.h"
#include "ntp_runtime_state.h"
#include "ota_runtime_state.h"
#include "ota_services.h"
#include "local_sensor_state.h"
#include "power_services.h"
#include "rtc_services.h"
#include "sensor_services.h"
#include "startup_state.h"
#include "ui_boot_screen.h"
#include "ui_display_flush.h"
#include "ui_info_page_state.h"
#include "ui_settings_feedback.h"
#include "ui_task.h"
#include "ui_task_notify.h"
#include "ui_work_page_catalog.h"
#include "weather_state.h"
#include "wifi_portal_state.h"
#include "xiaozhi_ai.h"

#include "i2c_equipment.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <stdlib.h>
#include <time.h>

#define MAIN_INVALID_TASK_CREATE_LOG_FORMAT "%s: invalid task create request"
#define MAIN_TASK_CREATE_FAILED_LOG_FORMAT "%s task create failed"
#define MAIN_NVS_INIT_REQUIRES_ERASE_LOG_FORMAT "nvs init requires erase: %s"
#define MAIN_NVS_ERASE_FAILED_LOG_FORMAT "nvs erase failed: %s"
#define MAIN_NVS_REINIT_FAILED_LOG_FORMAT "nvs re-init failed: %s"
#define MAIN_NVS_INIT_FAILED_LOG_FORMAT "nvs init failed: %s"
#define MAIN_EVENT_GROUP_CREATE_FAILED_LOG_FORMAT "app event group create failed"
#define MAIN_NETIF_INIT_FAILED_LOG_FORMAT "netif init failed: %s"
#define MAIN_EVENT_LOOP_INIT_FAILED_LOG_FORMAT "event loop init failed: %s"
#define MAIN_WEATHER_STATE_INIT_FAILED_LOG_FORMAT "weather state initialization failed"
#define MAIN_NETWORK_CREDENTIALS_STATE_INIT_FAILED_LOG_FORMAT "network credentials state initialization failed"
#define MAIN_NETWORK_DIAG_STATE_INIT_FAILED_LOG_FORMAT "network diagnostics state initialization failed"
#define MAIN_NTP_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT "NTP runtime state initialization failed"
#define MAIN_HOURLY_SENSOR_HISTORY_STATE_INIT_FAILED_LOG_FORMAT "hourly sensor history state initialization failed"
#define MAIN_LOCAL_SENSOR_STATE_INIT_FAILED_LOG_FORMAT "local sensor state initialization failed"
#define MAIN_OTA_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT "OTA runtime state initialization failed"
#define MAIN_DAILY_SAYING_STATE_INIT_FAILED_LOG_FORMAT "daily saying state initialization failed"
#define MAIN_MANUAL_WEATHER_CITY_STATE_INIT_FAILED_LOG_FORMAT "manual weather city state initialization failed"
#define MAIN_WEATHER_CITY_MCP_STATE_INIT_FAILED_LOG_FORMAT "weather city MCP state initialization failed"
#define MAIN_WIFI_PORTAL_STATE_INIT_FAILED_LOG_FORMAT "Wi-Fi portal state initialization failed"
#define MAIN_BATTERY_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT "battery runtime state initialization failed"
#define MAIN_INFO_PAGE_STATE_INIT_FAILED_LOG_FORMAT "info page state initialization failed"
#define MAIN_SETTINGS_FEEDBACK_STATE_INIT_FAILED_LOG_FORMAT "settings feedback state initialization failed"
#define MAIN_WORK_PAGE_CATALOG_INIT_FAILED_LOG_FORMAT "work page catalog initialization failed"
#define MAIN_POMODORO_STATE_INIT_FAILED_LOG_FORMAT "pomodoro runtime state initialization failed"
#define MAIN_ALARM_STATE_INIT_FAILED_LOG_FORMAT "alarm runtime state initialization failed"
#define MAIN_INVALID_BOOT_TASK_LOG_FORMAT "%s: invalid boot task request"
#define MAIN_BOOT_TASK_CREATE_FAILED_LOG_FORMAT "%s"
#define MAIN_DISPLAY_UNAVAILABLE_LOG_FORMAT "RLCD display resources unavailable; startup stopped"
#define MAIN_I2C_UNAVAILABLE_LOG_FORMAT "I2C master bus unavailable; startup stopped"
#define MAIN_LVGL_INIT_FAILED_LOG_FORMAT "LVGL initialization failed; startup stopped"

namespace {
constexpr uint32_t kBootAnimTaskStack = 6144;
constexpr uint32_t kBootSyncTaskStack = 20480;
constexpr uint32_t kNetworkSyncTaskStack = 20480;
constexpr uint32_t kOtaTaskStack = 16384;
constexpr uint32_t kHousekeepingTaskStack = 5120;
constexpr uint32_t kUiTaskStack = 8192;
constexpr uint32_t kButtonTaskStack = 3072;
constexpr uint32_t kAlarmTaskStack = 4096;
constexpr uint32_t kPomodoroTaskStack = 4096;
constexpr uint32_t kBootSyncWaitMarginMs = 500;
constexpr uint32_t kBootAnimStopWaitMs = 1500;
constexpr uint32_t kSetupPromptStartDelayMs = 350;
constexpr UBaseType_t kHighServiceTaskPriority = 4;
constexpr UBaseType_t kNormalServiceTaskPriority = 3;
constexpr UBaseType_t kInputTaskPriority = 2;
constexpr BaseType_t kNetworkTaskCore = 0;
constexpr BaseType_t kUiTaskCore = 1;
constexpr const char *kFallbackAppTaskName = "app_task";
constexpr const char *kFallbackBootTaskName = "boot_task";
constexpr const char *kBootAnimTaskName = "boot_anim_task";
constexpr const char *kBootSyncTaskName = "boot_sync";
constexpr const char *kNetworkSyncTaskName = "network_sync";
constexpr const char *kOtaTaskName = "ota_task";
constexpr const char *kHousekeepingTaskName = "housekeeping";
constexpr const char *kUiTaskName = "ui_task";
constexpr const char *kButtonTaskName = "button_task";
constexpr const char *kAlarmTaskName = "alarm_task";
constexpr const char *kPomodoroTaskName = "pomodoro_task";
constexpr const char *kBootAnimTaskCreateFailed = "boot animation task create failed";
constexpr const char *kBootConnectivityTaskCreateFailed = "boot connectivity task create failed";
constexpr const char *kBootReadyStatus = "Ready";
constexpr const char *kBootReadyDetail = "Starting clock";
struct AppTaskSpec {
    TaskFunction_t task;
    const char *name;
    uint32_t stack_depth;
    UBaseType_t priority;
    bool register_ui_handle;
    BaseType_t core_id;
};

struct AppInitializerSpec {
    bool (*initialize)();
    const char *failure_log;
};

constexpr AppTaskSpec kRegularAppTasks[] = {
    {network_sync_task, kNetworkSyncTaskName, kNetworkSyncTaskStack, kHighServiceTaskPriority, false, kNetworkTaskCore},
    {ota_task, kOtaTaskName, kOtaTaskStack, kHighServiceTaskPriority, false, kNetworkTaskCore},
    {housekeeping_task, kHousekeepingTaskName, kHousekeepingTaskStack, kNormalServiceTaskPriority, false, kUiTaskCore},
    {ui_task, kUiTaskName, kUiTaskStack, kNormalServiceTaskPriority, true, kUiTaskCore},
    {button_task, kButtonTaskName, kButtonTaskStack, kInputTaskPriority, false, kUiTaskCore},
    {alarm_task, kAlarmTaskName, kAlarmTaskStack, kNormalServiceTaskPriority, false, kUiTaskCore},
    {pomodoro_task, kPomodoroTaskName, kPomodoroTaskStack, kNormalServiceTaskPriority, false, kUiTaskCore},
};

constexpr AppInitializerSpec kCoreRuntimeStateInitializers[] = {
    {init_weather_state, MAIN_WEATHER_STATE_INIT_FAILED_LOG_FORMAT},
    {network_credentials_state_init, MAIN_NETWORK_CREDENTIALS_STATE_INIT_FAILED_LOG_FORMAT},
    {network_diagnostics_state_init, MAIN_NETWORK_DIAG_STATE_INIT_FAILED_LOG_FORMAT},
    {ntp_runtime_state_init, MAIN_NTP_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT},
    {init_hourly_sensor_history_state, MAIN_HOURLY_SENSOR_HISTORY_STATE_INIT_FAILED_LOG_FORMAT},
    {init_local_sensor_state, MAIN_LOCAL_SENSOR_STATE_INIT_FAILED_LOG_FORMAT},
    {daily_saying_state_init, MAIN_DAILY_SAYING_STATE_INIT_FAILED_LOG_FORMAT},
    {init_manual_weather_city_state, MAIN_MANUAL_WEATHER_CITY_STATE_INIT_FAILED_LOG_FORMAT},
    {wifi_portal_state_init, MAIN_WIFI_PORTAL_STATE_INIT_FAILED_LOG_FORMAT},
    {battery_runtime_state_init, MAIN_BATTERY_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT},
    {info_page_state_init, MAIN_INFO_PAGE_STATE_INIT_FAILED_LOG_FORMAT},
    {settings_feedback_state_init, MAIN_SETTINGS_FEEDBACK_STATE_INIT_FAILED_LOG_FORMAT},
    {work_page_catalog_init, MAIN_WORK_PAGE_CATALOG_INIT_FAILED_LOG_FORMAT},
};

constexpr AppInitializerSpec kFeatureRuntimeInitializers[] = {
    {alarm_services_init, MAIN_ALARM_STATE_INIT_FAILED_LOG_FORMAT},
    {pomodoro_services_init, MAIN_POMODORO_STATE_INIT_FAILED_LOG_FORMAT},
    {weather_city_mcp_init, MAIN_WEATHER_CITY_MCP_STATE_INIT_FAILED_LOG_FORMAT},
};

constexpr bool app_task_specs_valid()
{
    for (const AppTaskSpec &spec : kRegularAppTasks) {
        if (!spec.task || !spec.name || spec.name[0] == '\0' ||
            spec.stack_depth == 0 || spec.priority >= configMAX_PRIORITIES ||
            spec.core_id < 0 || spec.core_id >= portNUM_PROCESSORS ||
            (spec.register_ui_handle && spec.task != ui_task)) {
            return false;
        }
    }
    return true;
}

template <size_t Count>
constexpr bool app_initializer_specs_valid(
    const AppInitializerSpec (&specs)[Count])
{
    for (const AppInitializerSpec &spec : specs) {
        if (!spec.initialize || !spec.failure_log ||
            spec.failure_log[0] == '\0') {
            return false;
        }
    }
    return true;
}

template <size_t Count>
bool initialize_app_states(const AppInitializerSpec (&specs)[Count])
{
    for (const AppInitializerSpec &spec : specs) {
        if (!spec.initialize()) {
            ESP_LOGE(TAG, "%s", spec.failure_log);
            return false;
        }
    }
    return true;
}

static_assert(array_count(kRegularAppTasks) > 0,
              "regular task table must not be empty");
static_assert(app_task_specs_valid(), "regular app task specs must be valid");
static_assert(array_count(kCoreRuntimeStateInitializers) > 0,
              "core runtime initializer table must not be empty");
static_assert(array_count(kFeatureRuntimeInitializers) > 0,
              "feature runtime initializer table must not be empty");
static_assert(app_initializer_specs_valid(kCoreRuntimeStateInitializers),
              "core runtime initializer specs must be valid");
static_assert(app_initializer_specs_valid(kFeatureRuntimeInitializers),
              "feature runtime initializer specs must be valid");

} // namespace

static TaskHandle_t create_app_task(TaskFunction_t task,
                                    const char *name,
                                    uint32_t stack_depth,
                                    UBaseType_t priority,
                                    BaseType_t core_id)
{
    const char *task_name = name ? name : kFallbackAppTaskName;
    if (!task || stack_depth == 0) {
        ESP_LOGE(TAG, MAIN_INVALID_TASK_CREATE_LOG_FORMAT, task_name);
        return nullptr;
    }
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(task, task_name, stack_depth, nullptr, priority, &handle, core_id) != pdPASS) {
        ESP_LOGE(TAG, MAIN_TASK_CREATE_FAILED_LOG_FORMAT, task_name);
        return nullptr;
    }
    return handle;
}

static void create_regular_app_tasks()
{
    for (const AppTaskSpec &task : kRegularAppTasks) {
        TaskHandle_t handle = create_app_task(task.task,
                                              task.name,
                                              task.stack_depth,
                                              task.priority,
                                              task.core_id);
        if (task.register_ui_handle) {
            register_ui_task_handle(handle);
        }
    }
}

static bool init_nvs_storage()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, MAIN_NVS_INIT_REQUIRES_ERASE_LOG_FORMAT, esp_err_to_name(ret));
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, MAIN_NVS_ERASE_FAILED_LOG_FORMAT, esp_err_to_name(ret));
            return false;
        }
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, MAIN_NVS_REINIT_FAILED_LOG_FORMAT, esp_err_to_name(ret));
            return false;
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, MAIN_NVS_INIT_FAILED_LOG_FORMAT, esp_err_to_name(ret));
        return false;
    }
    return true;
}

static bool init_system_event_services()
{
    if (app_event_group_ready()) {
        return true;
    }
    if (!app_event_group_init()) {
        ESP_LOGE(TAG, MAIN_EVENT_GROUP_CREATE_FAILED_LOG_FORMAT);
        return false;
    }
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, MAIN_NETIF_INIT_FAILED_LOG_FORMAT, esp_err_to_name(ret));
        app_event_group_release();
        return false;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, MAIN_EVENT_LOOP_INIT_FAILED_LOG_FORMAT, esp_err_to_name(ret));
        // ESP-IDF 5.5 does not support esp_netif_deinit(). Release the owned
        // event group and leave the TCP/IP stack inert instead of retaining a
        // stale global handle that later code could mistake for a usable bus.
        app_event_group_release();
        return false;
    }
    return true;
}

static void create_boot_task_or_signal(TaskFunction_t task,
                                       const char *name,
                                       uint32_t stack_depth,
                                       BaseType_t core_id,
                                       EventBits_t done_bit,
                                       const char *failure_log)
{
    const char *task_name = name ? name : kFallbackBootTaskName;
    if (!task || stack_depth == 0) {
        ESP_LOGW(TAG, MAIN_INVALID_BOOT_TASK_LOG_FORMAT, failure_log ? failure_log : task_name);
        app_event_group_set_bits(done_bit);
        return;
    }
    if (xTaskCreatePinnedToCore(task,
                                task_name,
                                stack_depth,
                                nullptr,
                                kHighServiceTaskPriority,
                                nullptr,
                                core_id) != pdPASS) {
        ESP_LOGW(TAG, MAIN_BOOT_TASK_CREATE_FAILED_LOG_FORMAT, failure_log);
        app_event_group_set_bits(done_bit);
    }
}

extern "C" void app_main(void)
{
    DisplayPort &display = app_display();
    I2cMasterBus &i2c = app_i2c();
    if (!display.IsReady()) {
        ESP_LOGE(TAG, MAIN_DISPLAY_UNAVAILABLE_LOG_FORMAT);
        return;
    }
    if (!i2c.IsReady()) {
        ESP_LOGE(TAG, MAIN_I2C_UNAVAILABLE_LOG_FORMAT);
        return;
    }
    if (!init_nvs_storage()) {
        return;
    }

    if (!ota_runtime_state_init()) {
        ESP_LOGE(TAG, MAIN_OTA_RUNTIME_STATE_INIT_FAILED_LOG_FORMAT);
        return;
    }
    ota_mark_running_app_valid();
    if (!init_system_event_services()) {
        return;
    }
    if (!init_network_http_transaction_lock()) {
        return;
    }
    if (!initialize_app_states(kCoreRuntimeStateInitializers)) {
        return;
    }
    init_power_management();
    load_hourly_sensor_history();
    load_daily_saying_cache();
    custom_assets_init();

    (void)load_saved_config();
    Rtc_Setup(&i2c, 0x51);
    setenv("TZ", "CST-8", 1);
    tzset();
    restore_system_time_from_rtc();
    init_shtc3_sensor(i2c);
    sample_battery();
    if (!battery_low_mode_load()) {
        sample_sensor();
    }
    init_wifi();
    park_unused_audio_peripherals();
    xiaozhi_ai_init();
    if (!initialize_app_states(kFeatureRuntimeInitializers)) {
        return;
    }

    display.RLCD_Init();
    if (!display.IsReady()) {
        ESP_LOGE(TAG, MAIN_DISPLAY_UNAVAILABLE_LOG_FORMAT);
        return;
    }
    display.RLCD_ColorClear(ColorWhite);
    display.RLCD_Display();
    if (!Lvgl_PortInit(kDisplayWidth, kDisplayHeight, flush_callback)) {
        ESP_LOGE(TAG, MAIN_LVGL_INIT_FAILED_LOG_FORMAT);
        return;
    }
    if (Lvgl_lock(-1)) {
        show_boot_screen();
        Lvgl_unlock();
    }
    prepare_boot_animation();
    app_event_group_clear_bits(kBootSyncDoneBit | kBootAnimDoneBit);
    create_boot_task_or_signal(boot_anim_task,
                               kBootAnimTaskName,
                               kBootAnimTaskStack,
                               kUiTaskCore,
                               kBootAnimDoneBit,
                               kBootAnimTaskCreateFailed);
    create_boot_task_or_signal(boot_connectivity_task,
                               kBootSyncTaskName,
                               kBootSyncTaskStack,
                               kNetworkTaskCore,
                               kBootSyncDoneBit,
                               kBootConnectivityTaskCreateFailed);
    app_event_group_wait_bits(kBootSyncDoneBit,
                              pdFALSE,
                              pdTRUE,
                              pdMS_TO_TICKS(kBootStartupBudgetMs + kBootSyncWaitMarginMs));
    update_boot_screen(100, kBootReadyStatus, kBootReadyDetail);
    request_boot_animation_stop();
    app_event_group_wait_bits(kBootAnimDoneBit,
                              pdFALSE,
                              pdTRUE,
                              pdMS_TO_TICKS(kBootAnimStopWaitMs));
    finish_boot_anim_to_last_frame();
    finish_boot_screen();
    startup_screen_mark_finished();

    create_regular_app_tasks();

    if (setup_prompt_playback_pending()) {
        vTaskDelay(pdMS_TO_TICKS(kSetupPromptStartDelayMs));
        if (setup_prompt_playback_pending()) {
            (void)start_setup_prompt_playback();
        }
    }
}
