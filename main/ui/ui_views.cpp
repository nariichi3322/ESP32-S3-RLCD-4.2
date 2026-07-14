// 运行 LVGL UI 主任务并统一调度各页面刷新。
#include "ui_views.h"

#include "ui_clock_alert_state.h"

#include "alarm_services.h"
#include "app_constexpr.h"
#include "app_tick_time.h"
#include "audio_services.h"
#include "network_services.h"
#include "ota_services.h"
#include "pomodoro_services.h"
#include "sensor_services.h"
#include "ui_battery.h"
#include "ui_battery_blink.h"
#include "ui_loop_schedule.h"
#include "ui_setup_status.h"
#include "ui_visible_data_sync.h"
#include "ui_xiaozhi_auto_return.h"
#include "xiaozhi_ai.h"

#include <stdint.h>

namespace {
constexpr int kUiStatusRefreshMs = 10000;
constexpr int kUiInfoPagePollMs = 250;
constexpr int kUiNetworkDiagRunningPollMs = 250;
constexpr int kUiNetworkDiagIdlePollMs = 500;
constexpr int kUiSettingsPollMs = 100;
constexpr int kUiPostPageSwitchPollMs = 250;
constexpr int kUiLvglLockTimeoutMs = 80;
constexpr const char *kUiDatePlaceholder = "----/--/-- / 星期-";
constexpr const char *kUiTimePlaceholder = "--:--";
#define UI_SETTINGS_TIMEOUT_RETURN_LOG "settings timeout, returning to clock"
#define UI_XIAOZHI_AUTO_RETURN_LOG "Xiaozhi idle timeout, returning to home page=%d"

constexpr const char *kUiFormatTexts[] = {
    kUiDatePlaceholder,
    kUiTimePlaceholder,
};
constexpr const char *kUiLogTexts[] = {
    UI_SETTINGS_TIMEOUT_RETURN_LOG,
    UI_XIAOZHI_AUTO_RETURN_LOG,
};

bool settings_timeout_elapsed(TickType_t last_activity)
{
    if (last_activity == 0) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(kSettingsTimeoutMs);
    return app_tick_interval_elapsed(now, last_activity, timeout_ticks);
}

void invalidate_work_page_time_cache()
{
    g_last_ui_second = -1;
    g_last_ui_minute = -1;
    g_last_ui_date_key = -1;
    g_last_ui_date_page = -1;
}

static_assert(kUiStatusRefreshMs > 0, "UI status refresh interval must be positive");
static_assert(kUiInfoPagePollMs > 0, "UI info page poll interval must be positive");
static_assert(kUiNetworkDiagRunningPollMs > 0, "network diagnostics running poll interval must be positive");
static_assert(kUiNetworkDiagIdlePollMs >= kUiNetworkDiagRunningPollMs,
              "idle network diagnostics polling must not be faster than running diagnostics polling");
static_assert(kUiSettingsPollMs > 0, "settings poll interval must be positive");
static_assert(kUiPostPageSwitchPollMs > 0, "post page switch poll interval must be positive");
static_assert(kUiLvglLockTimeoutMs > 0, "UI LVGL lock timeout must be positive");
static_assert(sizeof(TickType_t) == sizeof(uint32_t),
              "UI delay candidates require 32-bit FreeRTOS ticks");
static_assert(array_count(kUiFormatTexts) > 0, "UI format text registry must not be empty");
static_assert(array_count(kUiLogTexts) > 0, "UI log text registry must not be empty");
static_assert(cstr_array_nonempty(kUiFormatTexts), "UI status format and placeholder texts must be non-empty");
static_assert(cstr_array_nonempty(kUiLogTexts), "UI main-loop log texts must be non-empty");

} // namespace

void notify_ui_task()
{
    if (g_ui_task_handle) {
        xTaskNotifyGive(g_ui_task_handle);
    }
}

namespace {
TickType_t next_second_delay_ticks()
{
    return pdMS_TO_TICKS(ui_next_second_delay_ms(esp_timer_get_time()));
}

TickType_t next_minute_delay_ticks(const struct tm &local)
{
    return pdMS_TO_TICKS(ui_next_minute_delay_ms(local.tm_sec));
}

bool update_invalid_time_labels_for_active_page(int active_work_page)
{
    int status_page = (g_low_battery_mode || g_setup_portal_active)
                          ? kWorkPageWeatherClock
                          : active_work_page;
    WorkPageStatusLabels labels = get_work_page_status_labels(status_page);
    bool changed = set_label_text_if_changed(labels.date, kUiDatePlaceholder);
    changed |= set_label_text_if_changed(labels.time, kUiTimePlaceholder);
    return changed;
}

bool auxiliary_page_requested()
{
    return g_settings_requested ||
           g_boot_info_requested ||
           g_network_diag_page_requested;
}

bool low_refresh_work_page_idle(const struct tm &local)
{
    return work_page_uses_low_refresh_idle(g_active_work_page) &&
           !g_low_battery_mode &&
           !g_battery_charging &&
           !g_setup_portal_active &&
           !auxiliary_page_requested() &&
           is_tm_plausible(local);
}

bool flip_clock_fast_poll_active(const struct tm &local)
{
    return g_active_work_page == kWorkPageFlipClock &&
           !g_low_battery_mode &&
           !g_setup_portal_active &&
           !auxiliary_page_requested() &&
           is_tm_plausible(local);
}

TickType_t next_ui_loop_delay_ticks(const struct tm &local, bool battery_blink_visible)
{
    bool low_idle = g_low_battery_mode &&
                    !g_battery_charging &&
                    !auxiliary_page_requested() &&
                    is_tm_plausible(local);
    bool low_refresh_page_idle = low_refresh_work_page_idle(local);
    uint32_t delay_candidates[5] = {};
    delay_candidates[0] = (low_idle || low_refresh_page_idle)
                              ? next_minute_delay_ticks(local)
                              : next_second_delay_ticks();
    if (flip_clock_fast_poll_active(local)) {
        delay_candidates[1] = pdMS_TO_TICKS(kUiLoopFlipClockPollMs);
    }
    if (normal_work_page_active(kWorkPageXiaozhiAI)) {
        PomodoroSnapshot pomodoro = {};
        pomodoro_get_snapshot(&pomodoro);
        if (pomodoro.state == kPomodoroRunning) {
            uint32_t boundary_ms = pomodoro_next_display_boundary_ms(pomodoro.remaining_ms);
            if (boundary_ms > 0) {
                delay_candidates[2] = ui_nonzero_delay_ticks(
                    pdMS_TO_TICKS(ui_pomodoro_boundary_delay_ms(boundary_ms)));
            }
        }
        uint32_t subtitle_delay_ms = xiaozhi_subtitle_animation_delay_ms();
        if (subtitle_delay_ms > 0) {
            delay_candidates[3] = ui_nonzero_delay_ticks(pdMS_TO_TICKS(subtitle_delay_ms));
        }
    }
    if (battery_blink_visible) {
        delay_candidates[4] = next_second_delay_ticks();
    }
    return static_cast<TickType_t>(
        ui_shortest_delay_ticks(delay_candidates, array_count(delay_candidates)));
}

void update_xiaozhi_auto_return_state(TickType_t tick_now,
                                      TickType_t &last_activity_tick,
                                      uint32_t &last_activity_sequence)
{
    if (g_active_work_page == kWorkPageXiaozhiAI &&
        !g_low_battery_mode &&
        !g_setup_portal_active &&
        !auxiliary_page_requested()) {
        XiaozhiAiSnapshot snapshot = {};
        xiaozhi_ai_get_snapshot(&snapshot);
        bool conversation_active = snapshot.state == kXiaozhiAiListening ||
                                   snapshot.state == kXiaozhiAiSpeaking;
        XiaozhiAutoReturnDecision auto_return = xiaozhi_auto_return_decision(
            tick_now,
            last_activity_tick,
            pdMS_TO_TICKS(kXiaozhiAutoReturnTimeoutMs),
            g_xiaozhi_auto_return_enabled,
            pomodoro_is_running(),
            conversation_active,
            snapshot.activity_sequence != last_activity_sequence);
        if (auto_return.record_activity) {
            last_activity_tick = tick_now;
            last_activity_sequence = snapshot.activity_sequence;
        } else if (auto_return.return_home) {
            int home_page = first_enabled_work_page();
            if (home_page != kWorkPageXiaozhiAI) {
                ESP_LOGI(TAG, UI_XIAOZHI_AUTO_RETURN_LOG, home_page);
                g_active_work_page = home_page;
            }
            last_activity_tick = tick_now;
        }
    } else if (g_active_work_page != kWorkPageXiaozhiAI) {
        last_activity_tick = 0;
        last_activity_sequence = 0;
    }
}

bool update_visible_work_page_body(const struct tm &local,
                                   const ActiveWorkPageState &state)
{
    bool changed = false;
    if (state.history) {
        changed |= update_history_page(local);
    }
    if (state.gallery) {
        changed |= update_gallery_page(local);
    }
    if (state.calendar) {
        changed |= update_calendar_page(local);
    }
    if (state.weather_board) {
        changed |= update_weather_board_page(local);
    }
    if (state.flip_clock) {
        changed |= update_flip_clock_page(local);
    }
    if (state.xiaozhi) {
        changed |= update_xiaozhi_page(local);
    }
    return changed;
}

bool update_weather_alert_state(const struct tm &local,
                                const ActiveWorkPageState &state,
                                bool status_due,
                                bool &alert_visible,
                                int &alert_index)
{
    if (!state.weather_clock) {
        if (!alert_visible) {
            return false;
        }
        update_alert_pill(false);
        alert_visible = false;
        alert_index = -1;
        return true;
    }

    WeatherAlertData alert = {};
    get_weather_snapshot(nullptr, &alert);
    ClockAlertDisplayState next_alert = clock_alert_display_state(local.tm_sec,
                                                                 g_low_battery_mode,
                                                                 alert.active,
                                                                 alert.count);
    if (!clock_alert_display_needs_update(next_alert,
                                          alert_visible,
                                          alert_index,
                                          status_due)) {
        return false;
    }
    update_alert_pill(next_alert.visible, next_alert.index);
    alert_visible = next_alert.visible;
    alert_index = next_alert.visible ? next_alert.index : -1;
    return true;
}

void show_boot_info_aux_page(bool &info_page_visible,
                             bool &settings_page_visible)
{
    build_boot_info_page();
    show_page(g_info_root);
    info_page_visible = true;
    settings_page_visible = false;
}

void show_network_diag_aux_page(bool &network_diag_page_visible,
                                bool &info_page_visible,
                                bool &settings_page_visible)
{
    build_network_diag_page();
    show_page(g_network_diag_root);
    network_diag_page_visible = true;
    info_page_visible = false;
    settings_page_visible = false;
}

bool update_active_work_page_content(struct tm &local,
                                     const ActiveWorkPageState &state,
                                     bool status_due,
                                     bool &alert_visible,
                                     int &alert_index)
{
    bool changed = false;
    if (is_system_time_plausible(&local)) {
        changed |= update_time_ui(local, state.weather_clock, g_active_work_page);
        changed |= update_visible_work_page_body(local, state);
        changed |= update_work_page_day_progress(g_active_work_page, local);
        changed |= update_weather_alert_state(local,
                                              state,
                                              status_due,
                                              alert_visible,
                                              alert_index);
        return changed;
    }

    changed |= update_invalid_time_labels_for_active_page(g_active_work_page);
    g_last_ui_date_key = -1;
    g_last_ui_date_page = -1;
    update_alert_pill(false);
    if (alert_visible) {
        alert_visible = false;
        alert_index = -1;
        changed = true;
    }
    return changed;
}
} // namespace

void ui_task(void *)
{
    TickType_t last_status_update = xTaskGetTickCount() - pdMS_TO_TICKS(kUiStatusRefreshMs);
    uint32_t last_battery_version = (uint32_t)-1;
    uint32_t last_alarm_version = 0;
    bool info_page_visible = false;
    bool network_diag_page_visible = false;
    bool settings_page_visible = false;
    bool setup_panel_visible = false;
    bool low_mode_visible = false;
    bool alert_visible = false;
    int visible_work_page = kWorkPageWeatherClock;
    int alert_index = -1;
    bool last_battery_charging = false;
    int last_battery_blink_phase = -1;
    uint32_t last_settings_action_seq = g_settings_action_seq;
    VisibleSyncRetryState<TickType_t> weather_sync_retry;
    VisibleSyncRetryState<TickType_t> saying_sync_retry;
    TickType_t xiaozhi_last_activity_tick = 0;
    uint32_t last_xiaozhi_activity_sequence = 0;

    for (;;) {
        time_t now;
        time(&now);
        struct tm local = {};
        localtime_r(&now, &local);
        if (!g_low_battery_mode && !g_setup_portal_active) {
            ensure_active_work_page_enabled();
        }
        xiaozhi_ai_set_page_active(g_active_work_page == kWorkPageXiaozhiAI &&
                                   !g_low_battery_mode &&
                                   !g_setup_portal_active &&
                                   !auxiliary_page_requested());

        TickType_t tick_now = xTaskGetTickCount();
        if (g_active_work_page == kWorkPageXiaozhiAI && auxiliary_page_requested()) {
            xiaozhi_last_activity_tick = tick_now;
        }
        bool status_due = app_tick_interval_elapsed(tick_now,
                                                    last_status_update,
                                                    pdMS_TO_TICKS(kUiStatusRefreshMs));
        uint32_t current_alarm_version = alarm_state_version();
        if (current_alarm_version != last_alarm_version) {
            status_due = true;
        }
        bool battery_due = g_battery_version != last_battery_version;
        UiBatteryBlinkState battery_blink = ui_battery_blink_state({
            g_battery_charging,
            g_battery_animation_complete,
            g_battery_percent,
            kBatteryChargingAnimationStopPercent,
            g_active_work_page,
            kWorkPageCount,
            g_setup_portal_active,
            auxiliary_page_requested(),
            is_tm_plausible(local),
            local.tm_sec,
            xTaskGetTickCount() / pdMS_TO_TICKS(kAppMsPerSecond),
        });
        bool battery_blink_visible = battery_blink.visible;
        bool battery_blink_on = battery_blink.on;
        int battery_blink_phase = battery_blink.phase;
        bool battery_blink_due = battery_blink_visible != last_battery_charging ||
                                 (battery_blink_visible &&
                                  battery_blink_phase != last_battery_blink_phase);
        bool setup_due = g_setup_portal_active != setup_panel_visible;
        bool mode_due = g_low_battery_mode != low_mode_visible;

        if (Lvgl_lock(kUiLvglLockTimeoutMs)) {
            bool refresh_now = false;
            bool info_requested = g_boot_info_requested;
            bool network_diag_requested = g_network_diag_page_requested;
            bool settings_requested = g_settings_requested;
            TickType_t info_until = g_info_page_until_tick;
            auto restore_active_work_page_after_aux = [&](bool clear_info_timeout) {
                show_active_work_page();
                if (clear_info_timeout) {
                    g_info_page_until_tick = 0;
                }
                visible_work_page = g_active_work_page;
                setup_panel_visible = false;
                low_mode_visible = g_low_battery_mode;
                apply_clock_mode_visibility(false);
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                invalidate_work_page_time_cache();
                refresh_now = true;
            };
            if (info_requested && info_until != 0 &&
                app_tick_deadline_reached(tick_now, info_until) &&
                !ota_flow_active()) {
                g_boot_info_requested = false;
                g_info_page_until_tick = 0;
                info_requested = false;
            }
            if (g_low_battery_mode && !ota_flow_active() &&
                (info_requested ||
                 network_diag_requested ||
                 settings_requested ||
                 g_active_work_page != kWorkPageWeatherClock)) {
                g_boot_info_requested = false;
                g_network_diag_page_requested = false;
                g_settings_requested = false;
                reset_settings_navigation_state();
                g_info_page_until_tick = 0;
                info_requested = false;
                network_diag_requested = false;
                settings_requested = false;
                info_page_visible = false;
                network_diag_page_visible = false;
                settings_page_visible = false;
                g_active_work_page = kWorkPageWeatherClock;
                show_active_work_page();
                visible_work_page = kWorkPageWeatherClock;
                setup_panel_visible = false;
                low_mode_visible = g_low_battery_mode;
                apply_clock_mode_visibility(g_setup_portal_active);
                update_alert_pill(false);
                alert_visible = false;
                alert_index = -1;
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                invalidate_work_page_time_cache();
                refresh_now = true;
            }
            if (info_requested && !settings_requested) {
                if (!info_page_visible) {
                    show_boot_info_aux_page(info_page_visible,
                                            settings_page_visible);
                }
                update_boot_info_page();
                lv_refr_now(nullptr);
                Lvgl_unlock();
                vTaskDelay(pdMS_TO_TICKS(g_ota_state == kOtaUpdating ? kOtaStatusMinIntervalMs : kUiInfoPagePollMs));
                continue;
            }
            if (info_page_visible) {
                info_page_visible = false;
                restore_active_work_page_after_aux(true);
            }

            if (network_diag_requested &&
                g_network_diag_state == kNetworkDiagDone &&
                settings_timeout_elapsed(g_settings_last_activity_tick)) {
                g_network_diag_page_requested = false;
                network_diag_requested = false;
            }
            if (network_diag_requested && !settings_requested) {
                if (!network_diag_page_visible) {
                    show_network_diag_aux_page(network_diag_page_visible,
                                               info_page_visible,
                                               settings_page_visible);
                }
                if (update_network_diag_page()) {
                    lv_refr_now(nullptr);
                }
                Lvgl_unlock();
                vTaskDelay(pdMS_TO_TICKS(g_network_diag_state == kNetworkDiagRunning ? kUiNetworkDiagRunningPollMs : kUiNetworkDiagIdlePollMs));
                continue;
            }
            if (network_diag_page_visible) {
                network_diag_page_visible = false;
                restore_active_work_page_after_aux(false);
            }

            if (settings_requested) {
                bool settings_changed = false;
                bool settings_action_handled = false;
                if (!settings_page_visible) {
                    build_settings_page();
                    show_page(g_settings_root);
                    settings_page_visible = true;
                    info_page_visible = false;
                    network_diag_page_visible = false;
                    setup_panel_visible = false;
                    settings_changed = true;
                }
                if (g_settings_action_seq != last_settings_action_seq) {
                    last_settings_action_seq = g_settings_action_seq;
                    handle_settings_action();
                    settings_changed = true;
                    settings_action_handled = true;
                    settings_requested = g_settings_requested;
                    if (!settings_requested && g_boot_info_requested) {
                        show_boot_info_aux_page(info_page_visible,
                                                settings_page_visible);
                        update_boot_info_page();
                        lv_refr_now(nullptr);
                        Lvgl_unlock();
                        vTaskDelay(pdMS_TO_TICKS(kUiPostPageSwitchPollMs));
                        continue;
                    }
                    if (!settings_requested && g_network_diag_page_requested) {
                        show_network_diag_aux_page(network_diag_page_visible,
                                                   info_page_visible,
                                                   settings_page_visible);
                        update_network_diag_page();
                        lv_refr_now(nullptr);
                        Lvgl_unlock();
                        vTaskDelay(pdMS_TO_TICKS(kUiPostPageSwitchPollMs));
                        continue;
                    }
                }
                if (settings_requested && finish_settings_sync_if_timed_out(tick_now)) {
                    settings_changed = true;
                }
                if (settings_requested) {
                    TickType_t last_activity = g_settings_last_activity_tick;
                    bool button_pressed = gpio_get_level(kBootButtonGpio) == 0 ||
                                          gpio_get_level(kKeyButtonGpio) == 0;
                    if (!settings_action_handled &&
                        !button_pressed &&
                        !is_settings_sync_busy() && !ota_flow_active() &&
                        settings_timeout_elapsed(last_activity)) {
                        ESP_LOGI(TAG, "%s", UI_SETTINGS_TIMEOUT_RETURN_LOG);
                        if (g_settings_page_order_mode) {
                            if (save_work_page_order()) {
                                g_active_work_page = first_enabled_work_page();
                            }
                        }
                        g_settings_requested = false;
                        reset_settings_navigation_state();
                        settings_requested = false;
                    }
                }
                if (settings_requested) {
                    if (update_settings_page() || settings_changed) {
                        lv_refr_now(nullptr);
                    }
                    Lvgl_unlock();
                    vTaskDelay(pdMS_TO_TICKS(g_ota_state == kOtaUpdating ? kOtaStatusMinIntervalMs : kUiSettingsPollMs));
                    continue;
                }
            }

            if (settings_page_visible) {
                settings_page_visible = false;
                restore_active_work_page_after_aux(false);
            }

            if (g_low_battery_mode || g_setup_portal_active) {
                if (g_active_work_page != kWorkPageWeatherClock) {
                    g_active_work_page = kWorkPageWeatherClock;
                }
            } else {
                ensure_active_work_page_enabled();
            }
            update_xiaozhi_auto_return_state(tick_now,
                                              xiaozhi_last_activity_tick,
                                              last_xiaozhi_activity_sequence);
            if (visible_work_page != g_active_work_page) {
                show_active_work_page();
                visible_work_page = g_active_work_page;
                xiaozhi_ai_set_page_active(visible_work_page == kWorkPageXiaozhiAI);
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                g_last_history_drawn_version = (uint32_t)-1;
                g_last_history_drawn_hour = -1;
                g_last_flip_clock_hour = -1;
                g_last_flip_clock_minute = -1;
                g_last_flip_clock_second = -1;
                g_last_flip_sensor_minute = -1;
                g_last_ui_date_key = -1;
                g_last_ui_date_page = -1;
                refresh_now = true;
            }
            const ActiveWorkPageState active_pages = active_work_page_state();
            update_visible_weather_sync(active_pages,
                                        now,
                                        tick_now,
                                        weather_sync_retry);
            update_visible_daily_saying_sync(active_pages,
                                             local,
                                             now,
                                             tick_now,
                                             saying_sync_retry);

            refresh_now |= update_active_work_page_content(local,
                                                           active_pages,
                                                           status_due,
                                                           alert_visible,
                                                           alert_index);

            if (status_due || battery_due || battery_blink_due || setup_due || mode_due) {
                EventBits_t bits = xEventGroupGetBits(g_app_events);
                bool setup_active = g_setup_portal_active;
                bool content_changed = false;
                if (setup_active != setup_panel_visible || mode_due) {
                    apply_clock_mode_visibility(setup_active);
                    setup_panel_visible = setup_active;
                    low_mode_visible = g_low_battery_mode;
                    status_due = true;
                    invalidate_work_page_time_cache();
                    g_last_second_progress_filled = -1;
                    update_alert_pill(false);
                    alert_visible = false;
                    alert_index = -1;
                    refresh_now = true;
                }
                if (setup_active) {
                    content_changed |= update_setup_status_panel();
                }
                if (!setup_active && !g_low_battery_mode && active_pages.weather_clock) {
                    content_changed |= update_weather_clock_sensor_status();
                    content_changed |= update_weather_clock_network_status(bits,
                                                                            now,
                                                                            tick_now,
                                                                            weather_sync_retry);
                }
                if (battery_due || battery_blink_due) {
                    update_work_page_battery_icon(g_active_work_page,
                                                  g_battery_percent,
                                                  battery_blink_visible,
                                                  battery_blink_on);
                    last_battery_version = g_battery_version;
                    last_battery_charging = battery_blink_visible;
                    last_battery_blink_phase = battery_blink_phase;
                    content_changed = true;
                }
                if (status_due) {
                    if (!active_pages.weather_clock) {
                        content_changed |= update_non_clock_work_page_sensor_status(g_active_work_page);
                    }
                    if (active_pages.weather_clock) {
                        content_changed |= update_top_status_icons(alert_visible);
                    } else {
                        content_changed |= update_work_page_status_icons(g_active_work_page);
                    }
                    last_status_update = tick_now;
                    last_alarm_version = current_alarm_version;
                }
                refresh_now |= content_changed;
            }
            if (refresh_now) {
                lv_refr_now(nullptr);
            }
            Lvgl_unlock();
        }
        TickType_t delay_ticks = next_ui_loop_delay_ticks(local, battery_blink_visible);
        ulTaskNotifyTake(pdTRUE, delay_ticks);
    }
}
