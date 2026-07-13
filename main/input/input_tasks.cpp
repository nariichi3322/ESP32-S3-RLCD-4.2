// 处理 BOOT 和 KEY 按键输入、页面切换和设置页操作请求。
#include "input_tasks.h"

#include "alarm_services.h"
#include "app_constexpr.h"
#include "audio_services.h"
#include "ota_services.h"
#include "pomodoro_services.h"
#include "ui_views.h"

#define BUTTON_GPIO_CONFIG_FAILED_LOG_FORMAT "button gpio config failed: %s"
#define BUTTON_SWITCH_WORK_PAGE_LOG_FORMAT "switch work page: %d"
#define BUTTON_SHOW_SETTINGS_LOG_FORMAT "key button clicked, showing settings page"

namespace {
constexpr const char *const kButtonLogTexts[] = {
    BUTTON_GPIO_CONFIG_FAILED_LOG_FORMAT,
    BUTTON_SWITCH_WORK_PAGE_LOG_FORMAT,
    BUTTON_SHOW_SETTINGS_LOG_FORMAT,
};
constexpr int kButtonDebounceMs = 18;
constexpr int kButtonLongPressMs = 1200;
constexpr int kButtonBusyFeedbackMs = 2000;
constexpr uint64_t kBootButtonPinMask = 1ULL << kBootButtonGpio;
constexpr uint64_t kKeyButtonPinMask = 1ULL << kKeyButtonGpio;
constexpr uint64_t kButtonInputPinMask = kBootButtonPinMask | kKeyButtonPinMask;
constexpr TickType_t kButtonDebounceTicks = pdMS_TO_TICKS(kButtonDebounceMs);
constexpr TickType_t kButtonLongPressTicks = pdMS_TO_TICKS(kButtonLongPressMs);
constexpr const char *kSettingsBusyFeedbackText = "请等待操作完成";

static_assert(kButtonDebounceMs > 0, "button debounce duration must be positive");
static_assert(kButtonLongPressMs > kButtonDebounceMs,
              "button long-press duration must be longer than debounce duration");
static_assert(kButtonBusyFeedbackMs > 0, "button busy feedback duration must be positive");
static_assert(array_count(kButtonLogTexts) > 0,
              "button log guard must cover button log texts");
static_assert(cstr_array_nonempty(kButtonLogTexts), "button task log texts must be non-empty");
static_assert(kBootButtonPinMask != 0, "BOOT button pin mask must not be empty");
static_assert(kKeyButtonPinMask != 0, "KEY button pin mask must not be empty");
static_assert(kButtonInputPinMask == (kBootButtonPinMask | kKeyButtonPinMask),
              "button input pin mask must include BOOT and KEY");
static_assert(kButtonDebounceTicks > 0, "button debounce tick duration must be positive");
static_assert(kButtonLongPressTicks > kButtonDebounceTicks,
              "button long-press tick duration must be longer than debounce duration");
static_assert(kButtonIdlePollMs > 0, "button idle poll interval must be positive");
static_assert(kButtonLowRefreshIdlePollMs > 0, "button low-refresh poll interval must be positive");
static_assert(kButtonActivePollMs > 0, "button active poll interval must be positive");
static_assert(kButtonPressedPollMs > 0, "button pressed poll interval must be positive");
static_assert(kButtonLowRefreshIdlePollMs <= kButtonIdlePollMs,
              "low-refresh button polling must stay at least as responsive as idle polling");
static_assert(kButtonActivePollMs <= kButtonIdlePollMs,
              "active button polling must stay at least as responsive as idle polling");
static_assert(kButtonPressedPollMs <= kButtonActivePollMs,
              "pressed button polling must stay at least as responsive as active polling");

bool button_press_is_short(TickType_t held)
{
    return held >= kButtonDebounceTicks &&
           held < kButtonLongPressTicks;
}

bool button_press_is_long(TickType_t held)
{
    return held >= kButtonLongPressTicks;
}

bool low_refresh_button_idle_context()
{
    if (g_battery_charging ||
        g_setup_portal_active ||
        g_settings_requested ||
        g_boot_info_requested ||
        g_network_diag_page_requested ||
        ota_flow_active() ||
        is_audio_playing() ||
        g_wifi_radio_on) {
        return false;
    }
    if (g_low_battery_mode) {
        return true;
    }
    return work_page_uses_low_refresh_idle(g_active_work_page);
}

void return_to_system_settings_item(int selection, TickType_t now)
{
    g_settings_requested = true;
    g_settings_focus_secondary = true;
    g_settings_page_toggle_mode = false;
    g_settings_page_order_mode = false;
    g_settings_primary_selection = kSettingsPrimarySystem;
    g_settings_selection = selection;
    g_settings_page_order_selection = 0;
    g_settings_last_activity_tick = now;
}

void enter_settings_primary_menu(TickType_t now)
{
    g_boot_info_requested = false;
    g_settings_requested = true;
    g_settings_focus_secondary = false;
    g_settings_page_toggle_mode = false;
    g_settings_page_order_mode = false;
    g_settings_primary_selection = kSettingsPrimaryNetwork;
    g_settings_selection = 0;
    g_settings_page_order_selection = 0;
    g_settings_last_activity_tick = now;
}

void handle_settings_key_long_or_busy()
{
    if (!is_settings_sync_busy() && !ota_flow_active()) {
        handle_settings_key_long();
        return;
    }
    set_settings_feedback(kSettingsBusyFeedbackText, kButtonBusyFeedbackMs);
}
} // namespace

void button_task(void *)
{
    gpio_config_t button = {};
    button.intr_type = GPIO_INTR_DISABLE;
    button.mode = GPIO_MODE_INPUT;
    button.pin_bit_mask = kButtonInputPinMask;
    button.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t err = gpio_config(&button);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, BUTTON_GPIO_CONFIG_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return;
    }

    TickType_t boot_pressed_since = 0;
    TickType_t key_pressed_since = 0;
    bool key_press_opened_settings = false;
    bool key_long_handled = false;
    bool boot_press_stopped_alert = false;
    bool key_press_stopped_alert = false;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool boot_pressed = gpio_get_level(kBootButtonGpio) == 0;
        bool key_pressed = gpio_get_level(kKeyButtonGpio) == 0;

        if (boot_pressed) {
            if (boot_pressed_since == 0) {
                boot_pressed_since = now;
                boot_press_stopped_alert = alarm_stop_ringing_from_button() ||
                                           pomodoro_stop_alert_from_button();
                if (g_settings_requested) {
                    g_settings_last_activity_tick = now;
                }
            }
        } else {
            if (boot_pressed_since != 0 && boot_press_stopped_alert) {
                // 提醒音播放期间任意按键只负责停止音频，不继续执行原按键动作。
            } else if (boot_pressed_since != 0 && g_settings_requested) {
                TickType_t held = now - boot_pressed_since;
                if (button_press_is_short(held)) {
                    g_settings_action_seq = g_settings_action_seq + 1;
                    notify_ui_task();
                }
                g_settings_last_activity_tick = now;
            } else if (boot_pressed_since != 0 &&
                       !g_boot_info_requested &&
                       !g_network_diag_page_requested &&
                       !g_setup_portal_active &&
                       !g_low_battery_mode) {
                TickType_t held = now - boot_pressed_since;
                if (button_press_is_short(held)) {
                    g_active_work_page = next_enabled_work_page(g_active_work_page);
                    ESP_LOGI(TAG, BUTTON_SWITCH_WORK_PAGE_LOG_FORMAT, g_active_work_page + 1);
                    notify_ui_task();
                }
            }
            boot_pressed_since = 0;
            boot_press_stopped_alert = false;
        }

        if (key_pressed) {
            if (key_pressed_since == 0) {
                key_pressed_since = now;
                key_press_opened_settings = false;
                key_long_handled = false;
                key_press_stopped_alert = alarm_stop_ringing_from_button() ||
                                          pomodoro_stop_alert_from_button();
                if (g_settings_requested) {
                    g_settings_last_activity_tick = now;
                }
                if (!key_press_stopped_alert &&
                    !g_settings_requested && !g_boot_info_requested && !g_network_diag_page_requested) {
                    ESP_LOGI(TAG, BUTTON_SHOW_SETTINGS_LOG_FORMAT);
                    enter_settings_primary_menu(now);
                    key_press_opened_settings = true;
                    notify_ui_task();
                }
            } else if (!key_press_stopped_alert &&
                       !key_press_opened_settings &&
                       !key_long_handled &&
                       g_settings_requested &&
                       button_press_is_long(now - key_pressed_since)) {
                g_settings_last_activity_tick = now;
                handle_settings_key_long_or_busy();
                key_long_handled = true;
                notify_ui_task();
            } else if (!key_long_handled &&
                       g_boot_info_requested &&
                       !g_settings_requested &&
                       button_press_is_long(now - key_pressed_since)) {
                g_boot_info_requested = false;
                g_info_page_until_tick = 0;
                return_to_system_settings_item(kSystemSettingsInfoItem, now);
                key_long_handled = true;
                notify_ui_task();
            } else if (!key_long_handled &&
                       g_network_diag_page_requested &&
                       !g_settings_requested &&
                       button_press_is_long(now - key_pressed_since)) {
                g_network_diag_page_requested = false;
                return_to_system_settings_item(kSystemSettingsNetworkDiagItem, now);
                key_long_handled = true;
                notify_ui_task();
            }
        } else {
            if (key_pressed_since != 0 &&
                !key_press_stopped_alert &&
                !key_press_opened_settings && !key_long_handled && g_settings_requested) {
                TickType_t held = now - key_pressed_since;
                if (button_press_is_long(held)) {
                    g_settings_last_activity_tick = now;
                    handle_settings_key_long_or_busy();
                    notify_ui_task();
                } else if (button_press_is_short(held)) {
                    g_settings_last_activity_tick = now;
                    if (!is_settings_sync_busy() && !ota_flow_active()) {
                        handle_settings_key_short();
                    } else {
                        set_settings_feedback(kSettingsBusyFeedbackText, kButtonBusyFeedbackMs);
                        notify_ui_task();
                    }
                }
            }
            if (key_pressed_since != 0 && g_settings_requested) {
                g_settings_last_activity_tick = now;
            }
            key_pressed_since = 0;
            key_press_opened_settings = false;
            key_long_handled = false;
            key_press_stopped_alert = false;
        }
        int delay_ms = low_refresh_button_idle_context() ? kButtonLowRefreshIdlePollMs : kButtonIdlePollMs;
        if (boot_pressed || key_pressed) {
            delay_ms = kButtonPressedPollMs;
        } else if (g_settings_requested || g_boot_info_requested || g_network_diag_page_requested || g_setup_portal_active) {
            delay_ms = kButtonActivePollMs;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
