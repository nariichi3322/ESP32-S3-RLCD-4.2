// 组合刷新当前工作页的时间、正文、全天进度和天气预警。
#include "ui_work_page_update.h"

#include "sensor_time.h"
#include "codex_usage_state.h"
#include "ui_codex_usage.h"
#include "ui_clock_runtime.h"
#include "ui_clock_time.h"
#include "ui_draw_cache.h"
#include "ui_flip_clock.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_widgets.h"
#include "ui_work_pages.h"
#include "ui_work_status.h"
#include "ui_xiaozhi.h"
#include "work_page_ids.h"

namespace {
constexpr const char *kUiDatePlaceholder = "----/--/-- / 星期-";
constexpr const char *kUiTimePlaceholder = "--:--";
bool update_visible_work_page_body(const struct tm &local,
                                   const ClockUiTimeSnapshot &time_snapshot,
                                   const ActiveWorkPageState &state,
                                   bool codex_feature_enabled)
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
        changed |= update_flip_clock_page(local, time_snapshot);
    }
    if (state.xiaozhi) {
        changed |= update_xiaozhi_page(local);
    }
    if (state.codex_usage) {
        CodexUsageSnapshotView view{};
        if (codex_usage_snapshot_copy(&view)) {
            changed |= update_codex_usage_page(local,
                                               view,
                                               codex_feature_enabled);
        }
    }
    return changed;
}

} // namespace

bool update_active_work_page_invalid_time_labels(int active_work_page,
                                                 bool force_weather_clock_status)
{
    int status_page = force_weather_clock_status
                          ? kWorkPageWeatherClock
                          : active_work_page;
    WorkPageStatusLabels labels = get_work_page_status_labels(status_page);
    invalidate_work_page_status_time_cache(status_page);
    bool changed = set_label_text_if_changed(labels.date, kUiDatePlaceholder);
    changed |= set_label_text_if_changed(labels.time, kUiTimePlaceholder);
    return changed;
}

bool update_active_work_page_content(const struct tm &local,
                                     const ActiveWorkPageState &state,
                                     int active_page,
                                     bool status_due,
                                     const UiStatusRefreshSnapshot &status,
                                     bool low_battery_mode,
                                     bool setup_portal_active)
{
    bool changed = false;
    if (is_tm_plausible(local)) {
        const ClockUiTimeSnapshot time_snapshot = clock_ui_time_snapshot(local);
        changed |= update_time_ui(local,
                                  time_snapshot,
                                  state.weather_clock,
                                  active_page,
                                  status.chime_enabled,
                                  low_battery_mode,
                                  setup_portal_active);
        changed |= update_visible_work_page_body(local,
                                                 time_snapshot,
                                                 state,
                                                 status.codex_enabled);
        changed |= update_work_page_day_progress(active_page, time_snapshot);
        return changed;
    }

    changed |= update_active_work_page_invalid_time_labels(
        active_page,
        low_battery_mode || setup_portal_active);
    invalidate_clock_date_draw_cache();
    update_alert_pill(false,
                      0,
                      status,
                      low_battery_mode,
                      setup_portal_active);
    return changed;
}
