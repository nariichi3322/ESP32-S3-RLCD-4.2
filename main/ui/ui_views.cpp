// 运行 LVGL UI 主任务并统一调度各页面刷新。
#include "ui_views.h"

#include "alarm_services.h"
#include "app_constexpr.h"
#include "app_event_group.h"
#include "app_tick_time.h"
#include "audio_services.h"
#include "battery_policy.h"
#include "chime_runtime_state.h"
#include "local_sensor_state.h"
#include "device_settings_persistence.h"
#include "input_button_config.h"
#include "network_diagnostics_state.h"
#include "ota_download_policy.h"
#include "ota_runtime_state.h"
#include "ota_services.h"
#include "sensor_time.h"
#include "ui_battery.h"
#include "ui_battery_blink.h"
#include "ui_aux_pages.h"
#include "ui_boot_settings.h"
#include "ui_draw_cache.h"
#include "ui_info_page_state.h"
#include "ui_loop_schedule.h"
#include "ui_runtime_schedule.h"
#include "ui_setup_status.h"
#include "ui_status_refresh_policy.h"
#include "ui_settings_activity_state.h"
#include "ui_visible_data_sync.h"
#include "ui_work_page_update.h"
#include "xiaozhi_ai.h"

#include <stdint.h>

namespace {
constexpr int kUiInfoPageFallbackPollMs = 1000;
constexpr int kUiNetworkDiagRunningFallbackMs = 1000;
constexpr int kUiNetworkDiagIdleFallbackMs = 500;
constexpr int kUiSettingsPollMs = 100;
constexpr int kUiPostPageSwitchPollMs = 250;
constexpr int kUiLvglLockTimeoutMs = 80;
#define UI_SETTINGS_TIMEOUT_RETURN_LOG "settings timeout, returning to clock"

constexpr const char *kUiLogTexts[] = {
    UI_SETTINGS_TIMEOUT_RETURN_LOG,
};

static_assert(kUiInfoPageFallbackPollMs > 0,
              "UI info page fallback poll interval must be positive");
static_assert(kUiNetworkDiagRunningFallbackMs > 0,
              "network diagnostics running fallback interval must be positive");
static_assert(kUiNetworkDiagIdleFallbackMs > 0,
              "network diagnostics idle fallback interval must be positive");
static_assert(kUiSettingsPollMs > 0, "settings poll interval must be positive");
static_assert(kUiPostPageSwitchPollMs > 0, "post page switch poll interval must be positive");
static_assert(kUiLvglLockTimeoutMs > 0, "UI LVGL lock timeout must be positive");
static_assert(array_count(kUiLogTexts) > 0, "UI log text registry must not be empty");
static_assert(cstr_array_nonempty(kUiLogTexts), "UI main-loop log texts must be non-empty");

} // namespace

namespace {
void show_boot_info_aux_page(bool &info_page_visible,
                             bool &settings_page_visible)
{
    build_boot_info_page();
    show_page(auxiliary_page_root(AuxiliaryPage::kSystemInfo));
    info_page_visible = true;
    settings_page_visible = false;
}

void show_network_diag_aux_page(bool &network_diag_page_visible,
                                bool &info_page_visible,
                                bool &settings_page_visible)
{
    build_network_diag_page();
    show_page(auxiliary_page_root(AuxiliaryPage::kNetworkDiagnostics));
    network_diag_page_visible = true;
    info_page_visible = false;
    settings_page_visible = false;
}

} // namespace

void ui_task(void *)
{
    TickType_t last_status_update =
        xTaskGetTickCount() - pdMS_TO_TICKS(kUiStatusFallbackRefreshMs);
    UiStatusRefreshSnapshot last_status_snapshot = {};
    bool last_status_snapshot_valid = false;
    uint32_t last_battery_version = (uint32_t)-1;
    bool info_page_visible = false;
    bool network_diag_page_visible = false;
    bool settings_page_visible = false;
    bool setup_panel_visible = false;
    bool low_mode_visible = false;
    bool alert_visible = false;
    int visible_work_page = kWorkPageWeatherClock;
    int alert_index = -1;
    uint32_t alert_version = 0;
    bool last_battery_charging = false;
    int last_battery_blink_phase = -1;
    uint32_t last_settings_action_seq = settings_activity_action_sequence();
    VisibleSyncRetryState<TickType_t> weather_sync_retry;
    VisibleSyncRetryState<TickType_t> saying_sync_retry;
    TickType_t xiaozhi_last_activity_tick = 0;
    uint32_t last_xiaozhi_activity_sequence = 0;
    BatteryRuntimeSnapshot battery;
    bool battery_snapshot_valid = false;
    time_t cached_local_second = 0;
    struct tm cached_local = {};
    bool cached_local_valid = false;

    for (;;) {
        time_t now;
        time(&now);
        if (ui_local_time_cache_refresh_due(now,
                                            cached_local_second,
                                            cached_local_valid)) {
            struct tm converted = {};
            if (localtime_r(&now, &converted)) {
                cached_local = converted;
                cached_local_second = now;
                cached_local_valid = true;
            } else {
                cached_local = {};
                cached_local_valid = false;
            }
        }
        const struct tm &local = cached_local;
        UiRuntimeSurfaceSnapshot runtime_surfaces =
            ui_runtime_surface_snapshot_load();
        const uint32_t battery_version = battery_runtime_version_load();
        if (!battery_snapshot_valid || battery.version != battery_version) {
            battery_runtime_snapshot_load(&battery);
            battery_snapshot_valid = true;
        }
        if (!battery.low_battery_mode && !runtime_surfaces.setup_portal_active) {
            ensure_active_work_page_enabled();
        }
        int active_page = active_work_page_load();
        xiaozhi_ai_set_page_active(active_page == kWorkPageXiaozhiAI &&
                                   !battery.low_battery_mode &&
                                   !runtime_surfaces.setup_portal_active &&
                                   !runtime_surfaces.auxiliary_page_requested());

        TickType_t tick_now = xTaskGetTickCount();
        if (active_page == kWorkPageXiaozhiAI &&
            runtime_surfaces.auxiliary_page_requested()) {
            xiaozhi_last_activity_tick = tick_now;
        }
        UiStatusRefreshSnapshot current_status_snapshot = {
            local_sensor_state_version(),
            chime_runtime_any_enabled(),
            wifi_radio_on_for_status_icon(),
            alarm_is_enabled(),
        };
        bool status_fallback_elapsed = app_tick_interval_elapsed(
            tick_now,
            last_status_update,
            pdMS_TO_TICKS(kUiStatusFallbackRefreshMs));
        bool status_due = ui_status_refresh_due(current_status_snapshot,
                                                last_status_snapshot,
                                                last_status_snapshot_valid,
                                                status_fallback_elapsed);
        bool battery_due = battery.version != last_battery_version;
        UiBatteryBlinkState battery_blink = ui_battery_blink_state({
            battery.charging,
            battery.animation_complete,
            battery.percent,
            kBatteryChargingAnimationStopPercent,
            active_page,
            kWorkPageCount,
            runtime_surfaces.setup_portal_active,
            runtime_surfaces.auxiliary_page_requested(),
            is_tm_plausible(local),
            local.tm_sec,
            tick_now / pdMS_TO_TICKS(kAppMsPerSecond),
        });
        bool battery_blink_visible = battery_blink.visible;
        bool battery_blink_on = battery_blink.on;
        int battery_blink_phase = battery_blink.phase;
        bool battery_blink_due = battery_blink_visible != last_battery_charging ||
                                 (battery_blink_visible &&
                                  battery_blink_phase != last_battery_blink_phase);
        bool setup_due = runtime_surfaces.setup_portal_active != setup_panel_visible;
        bool mode_due = battery.low_battery_mode != low_mode_visible;

        bool start_setup_prompt_after_ui = false;
        if (Lvgl_lock(kUiLvglLockTimeoutMs)) {
            bool refresh_now = false;
            bool info_requested = runtime_surfaces.info_requested;
            InfoPageStateSnapshot info_state = {};
            if (info_requested) {
                info_page_state_load(&info_state);
                info_requested = info_state.requested;
            }
            bool network_diag_requested = runtime_surfaces.network_diag_requested;
            bool settings_requested = runtime_surfaces.settings_requested;
            TickType_t info_until = info_state.hold_until_tick;
            auto restore_active_work_page_after_aux = [&](bool clear_info_timeout) {
                show_active_work_page();
                if (clear_info_timeout) {
                    info_page_hold_until_store(0);
                }
                active_page = active_work_page_load();
                visible_work_page = active_page;
                setup_panel_visible = false;
                low_mode_visible = battery.low_battery_mode;
                apply_clock_mode_visibility(false,
                                            battery.low_battery_mode);
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                invalidate_clock_time_draw_cache();
                refresh_now = true;
            };
            if (info_requested && info_until != 0 &&
                app_tick_deadline_reached(tick_now, info_until) &&
                !ota_flow_active()) {
                info_page_clear();
                info_requested = false;
                runtime_surfaces.info_requested = false;
            }
            if (battery.low_battery_mode && !ota_flow_active() &&
                (info_requested ||
                 network_diag_requested ||
                 settings_requested ||
                 active_page != kWorkPageWeatherClock)) {
                info_page_clear();
                network_diag_page_clear();
                settings_page_clear();
                reset_settings_navigation_state();
                info_requested = false;
                network_diag_requested = false;
                settings_requested = false;
                runtime_surfaces.info_requested = false;
                runtime_surfaces.network_diag_requested = false;
                runtime_surfaces.settings_requested = false;
                info_page_visible = false;
                network_diag_page_visible = false;
                settings_page_visible = false;
                active_work_page_store(kWorkPageWeatherClock);
                active_page = kWorkPageWeatherClock;
                show_active_work_page();
                visible_work_page = kWorkPageWeatherClock;
                setup_panel_visible = false;
                low_mode_visible = battery.low_battery_mode;
                apply_clock_mode_visibility(runtime_surfaces.setup_portal_active,
                                            battery.low_battery_mode);
                update_alert_pill(false,
                                  0,
                                  current_status_snapshot,
                                  battery.low_battery_mode,
                                  runtime_surfaces.setup_portal_active);
                alert_visible = false;
                alert_index = -1;
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                invalidate_clock_time_draw_cache();
                refresh_now = true;
            }
            if (info_requested && !settings_requested) {
                bool info_changed = false;
                if (!info_page_visible) {
                    show_boot_info_aux_page(info_page_visible,
                                            settings_page_visible);
                    info_changed = true;
                }
                info_changed |= update_boot_info_page();
                if (info_changed) {
                    lv_refr_now(nullptr);
                }
                Lvgl_unlock();
                TickType_t info_wait = pdMS_TO_TICKS(
                    ota_runtime_state_load() == kOtaUpdating
                        ? kOtaStatusMinIntervalMs
                        : kUiInfoPageFallbackPollMs);
                if (info_until != 0 && !ota_flow_active()) {
                    TickType_t remaining = app_tick_deadline_remaining(xTaskGetTickCount(),
                                                                       info_until);
                    if (remaining < info_wait) {
                        info_wait = remaining;
                    }
                }
                (void)ulTaskNotifyTake(pdTRUE, info_wait);
                continue;
            }
            if (info_page_visible) {
                info_page_visible = false;
                restore_active_work_page_after_aux(true);
            }

            NetworkDiagState network_diag_state = network_diag_state_load();
            if (network_diag_requested &&
                network_diag_state == kNetworkDiagDone &&
                ui_runtime_settings_timeout_elapsed(
                    settings_activity_last_tick())) {
                network_diag_page_clear();
                network_diag_requested = false;
                runtime_surfaces.network_diag_requested = false;
            }
            if (network_diag_requested && !settings_requested) {
                bool network_diag_changed = false;
                if (!network_diag_page_visible) {
                    show_network_diag_aux_page(network_diag_page_visible,
                                               info_page_visible,
                                               settings_page_visible);
                    network_diag_changed = true;
                }
                network_diag_changed |= update_network_diag_page();
                if (network_diag_changed) {
                    lv_refr_now(nullptr);
                }
                Lvgl_unlock();
                TickType_t network_diag_wait = pdMS_TO_TICKS(
                    network_diag_state_load() == kNetworkDiagRunning
                        ? kUiNetworkDiagRunningFallbackMs
                        : kUiNetworkDiagIdleFallbackMs);
                (void)ulTaskNotifyTake(pdTRUE, network_diag_wait);
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
                    show_page(auxiliary_page_root(AuxiliaryPage::kSettings));
                    settings_page_visible = true;
                    info_page_visible = false;
                    network_diag_page_visible = false;
                    setup_panel_visible = false;
                    settings_changed = true;
                }
                uint32_t settings_action_seq = settings_activity_action_sequence();
                if (settings_action_seq != last_settings_action_seq) {
                    last_settings_action_seq = settings_action_seq;
                    handle_settings_action();
                    settings_changed = true;
                    settings_action_handled = true;
                    runtime_surfaces = ui_runtime_surface_snapshot_load();
                    settings_requested = runtime_surfaces.settings_requested;
                    if (!settings_requested && runtime_surfaces.info_requested) {
                        show_boot_info_aux_page(info_page_visible,
                                                settings_page_visible);
                        update_boot_info_page();
                        lv_refr_now(nullptr);
                        Lvgl_unlock();
                        vTaskDelay(pdMS_TO_TICKS(kUiPostPageSwitchPollMs));
                        continue;
                    }
                    if (!settings_requested && runtime_surfaces.network_diag_requested) {
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
                    TickType_t last_activity = settings_activity_last_tick();
                    bool button_pressed = gpio_get_level(kBootButtonGpio) == 0 ||
                                          gpio_get_level(kKeyButtonGpio) == 0;
                    if (!settings_action_handled &&
                        !button_pressed &&
                        !is_settings_sync_busy() && !ota_flow_active() &&
                        ui_runtime_settings_timeout_elapsed(last_activity)) {
                        ESP_LOGI(TAG, "%s", UI_SETTINGS_TIMEOUT_RETURN_LOG);
                        if (settings_navigation_snapshot().page_order_mode) {
                            if (save_work_page_order()) {
                                active_work_page_store(first_enabled_work_page());
                            }
                        }
                        settings_page_clear();
                        reset_settings_navigation_state();
                        settings_requested = false;
                        runtime_surfaces.settings_requested = false;
                    }
                }
                if (settings_requested) {
                    if (update_settings_page() || settings_changed) {
                        lv_refr_now(nullptr);
                    }
                    Lvgl_unlock();
                    vTaskDelay(pdMS_TO_TICKS(ota_runtime_state_load() == kOtaUpdating
                                                 ? kOtaStatusMinIntervalMs
                                                 : kUiSettingsPollMs));
                    continue;
                }
            }

            if (settings_page_visible) {
                settings_page_visible = false;
                restore_active_work_page_after_aux(false);
            }

            if (battery.low_battery_mode || runtime_surfaces.setup_portal_active) {
                if (active_page != kWorkPageWeatherClock) {
                    active_work_page_store(kWorkPageWeatherClock);
                }
            } else {
                ensure_active_work_page_enabled();
            }
            active_page = active_work_page_load();
            ui_runtime_update_xiaozhi_auto_return(
                active_page,
                battery.low_battery_mode,
                runtime_surfaces,
                tick_now,
                xiaozhi_last_activity_tick,
                last_xiaozhi_activity_sequence);
            active_page = active_work_page_load();
            if (visible_work_page != active_page) {
                show_active_work_page();
                visible_work_page = active_page;
                xiaozhi_ai_set_page_active(visible_work_page == kWorkPageXiaozhiAI);
                status_due = true;
                battery_due = true;
                battery_blink_due = true;
                invalidate_history_draw_cache();
                invalidate_flip_clock_time_sensor_draw_cache();
                invalidate_clock_date_draw_cache();
                refresh_now = true;
            }
            const bool setup_active_for_frame = runtime_surfaces.setup_portal_active;
            const bool normal_mode_for_frame = !battery.low_battery_mode &&
                                               !setup_active_for_frame;
            const ActiveWorkPageState active_pages = active_work_page_state_for_mode(
                active_page,
                normal_mode_for_frame);
            if (!setup_active_for_frame) {
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
                                                               active_page,
                                                               status_due,
                                                               current_status_snapshot,
                                                               battery.low_battery_mode,
                                                               setup_active_for_frame,
                                                               alert_visible,
                                                               alert_index,
                                                               alert_version);
            } else if (is_tm_plausible(local)) {
                refresh_now |= update_setup_clock_header_time_ui(local);
            } else {
                refresh_now |= update_active_work_page_invalid_time_labels(
                    active_page,
                    battery.low_battery_mode || setup_active_for_frame);
            }

            if (status_due || battery_due || battery_blink_due || setup_due || mode_due) {
                EventBits_t bits = app_event_group_get_bits();
                bool setup_active = runtime_surfaces.setup_portal_active;
                bool content_changed = false;
                if (setup_active != setup_panel_visible || mode_due) {
                    apply_clock_mode_visibility(setup_active,
                                                battery.low_battery_mode);
                    setup_panel_visible = setup_active;
                    low_mode_visible = battery.low_battery_mode;
                    status_due = true;
                    invalidate_clock_time_draw_cache();
                    invalidate_clock_second_progress_draw_cache();
                    update_alert_pill(false,
                                      0,
                                      current_status_snapshot,
                                      battery.low_battery_mode,
                                      setup_active);
                    alert_visible = false;
                    alert_index = -1;
                    refresh_now = true;
                }
                if (setup_active) {
                    content_changed |= update_setup_status_panel();
                }
                if (!setup_active && !battery.low_battery_mode && active_pages.weather_clock) {
                    content_changed |= update_weather_clock_network_status(bits,
                                                                            now,
                                                                            tick_now,
                                                                            weather_sync_retry);
                }
                if (battery_due || battery_blink_due) {
                    update_work_page_battery_icon(active_page,
                                                  battery.percent,
                                                  battery_blink_visible,
                                                  battery_blink_on);
                    last_battery_version = battery.version;
                    last_battery_charging = battery_blink_visible;
                    last_battery_blink_phase = battery_blink_phase;
                    content_changed = true;
                }
                if (status_due) {
                    if (active_pages.weather_clock) {
                        if (!setup_active && !battery.low_battery_mode) {
                            content_changed |= update_weather_clock_sensor_status();
                        }
                    } else {
                        content_changed |= update_non_clock_work_page_sensor_status(active_page);
                    }
                    if (active_pages.weather_clock) {
                        content_changed |= update_top_status_icons(
                            alert_visible,
                            current_status_snapshot,
                            battery.low_battery_mode,
                            setup_active);
                    } else {
                        content_changed |= update_work_page_status_icons(
                            active_page,
                            current_status_snapshot,
                            battery.low_battery_mode,
                            setup_active);
                    }
                    last_status_update = tick_now;
                    last_status_snapshot = current_status_snapshot;
                    last_status_snapshot_valid = true;
                }
                refresh_now |= content_changed;
            }
            if (refresh_now) {
                lv_refr_now(nullptr);
            }
            start_setup_prompt_after_ui = setup_panel_visible &&
                                          setup_prompt_playback_pending();
            Lvgl_unlock();
        }
        if (start_setup_prompt_after_ui) {
            (void)start_setup_prompt_playback();
        }
        TickType_t delay_ticks = ui_runtime_next_loop_delay_ticks(
            local,
            now,
            battery,
            battery_blink_visible,
            active_page,
            runtime_surfaces);
        ulTaskNotifyTake(pdTRUE, delay_ticks);
    }
}
