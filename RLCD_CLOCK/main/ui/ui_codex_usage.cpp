#include "ui_codex_usage.h"

#include "battery_runtime_state.h"
#include "sensor_time.h"
#include "ui_battery.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_usage_bottom_mode.h"
#include "ui_usage_rows.h"
#include "ui_widgets.h"
#include "ui_work_status.h"
#include "work_page_ids.h"

#include <esp_timer.h>
#include <stdio.h>
#include <time.h>

namespace {
lv_obj_t *s_online_content;
lv_obj_t *s_offline_content;
UsageRow s_rows[3];
UsageBottom s_bottom;

bool update_bottom(const CodexUsageSnapshotView &view, uint32_t now, bool time_valid)
{
    const UsageBottomSection section = usage_bottom_section_load(UsagePage::Codex);
    const bool valid = view.snapshot_valid && view.data_valid;
    const char *message = nullptr;
    char left[80] = "";
    char right[96] = "";
    if (!valid) {
        message = ui_text(UiTextId::UiPlaceholder);
    } else if (!view.snapshot.reset_credits) {
        message = ui_text(UiTextId::UsageNoGrants);
    } else {
        char count[16];
        snprintf(count, sizeof(count), "%u", view.snapshot.reset_credits);
        snprintf(left, sizeof(left), ui_format(UiTextId::UsageGrantCount),
                 ui_text(UiTextId::UsageAvailable), count);
        const uint32_t expiry = codex_usage_countdown_seconds(
            view.snapshot.next_credit_expiry_seconds, view.received_tick_ms, now);
        const time_t wall_time = time_valid ? time(nullptr) : 0;
        if (!view.snapshot.next_credit_expiry_seconds || !expiry || wall_time <= 0) {
            snprintf(right, sizeof(right), "%s", ui_text(UiTextId::UiPlaceholder));
        } else {
            char date[32];
            usage_format_date(static_cast<uint64_t>(wall_time) + expiry, date, sizeof(date));
            snprintf(right, sizeof(right), ui_format(UiTextId::UsageNearestExpires), date);
        }
    }
    const UsageStats stats = usage_format_stats(view, true);
    return usage_bottom_update(s_bottom, section,
                               ui_text(section == UsageBottomSection::Stats
                                           ? UiTextId::UsageStats : UiTextId::UsageGrants),
                               left, right, "", "", message, false,
                               stats.today, stats.week, stats.running);
}
}

void build_codex_usage_page()
{
    if (work_page_root(kWorkPageCodexUsage)) return;
    lv_obj_t *screen = create_page_root();
    if (!screen) return;
    set_work_page_root(kWorkPageCodexUsage, screen);
    build_work_page_battery_icon(screen, kWorkPageCodexUsage);
    build_work_page_status_bar(screen, kWorkPageCodexUsage, true, true);
    make_black_bar(screen, 18, 65, 364, 2);
    s_online_content = usage_content_container(screen);
    s_offline_content = usage_content_container(screen);
    if (!s_online_content || !s_offline_content) return;
    s_rows[0] = usage_row_create(s_online_content, 8, ui_text(UiTextId::UsageFiveHours), true);
    s_rows[1] = usage_row_create(s_online_content, 56, ui_text(UiTextId::UsageSevenDays), true);
    s_rows[2] = usage_row_create(s_online_content, 104, ui_text(UiTextId::UsagePaid), false, 2);
    usage_bottom_create(s_online_content, s_bottom);
    lv_obj_t *offline_title = make_label_with_font(
        s_offline_content, 25, 56, 350, 34,
        ui_text(UiTextId::CodexBluetoothOff), ui_font(UiFontRole::Body16));
    lv_obj_t *offline_detail = make_label_with_font(
        s_offline_content, 25, 96, 350, 24,
        ui_text(UiTextId::CodexOffline), ui_font(UiFontRole::Body16));
    if (offline_title) lv_obj_set_style_text_align(offline_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    if (offline_detail) lv_obj_set_style_text_align(offline_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    set_obj_visible(s_online_content, true);
    set_obj_visible(s_offline_content, false);
    build_work_page_day_progress(screen, kWorkPageCodexUsage);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageCodexUsage, battery_percent_load());
}

bool update_codex_usage_page(const struct tm &local,
                             const CodexUsageSnapshotView &view,
                             bool feature_enabled)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const bool time_valid = is_tm_plausible(local);
    bool changed = time_valid ? update_work_page_status_time(kWorkPageCodexUsage, local) : false;
    changed |= set_obj_visible(s_online_content, feature_enabled);
    changed |= set_obj_visible(s_offline_content, !feature_enabled);
    if (!feature_enabled) return changed;
    const bool valid = view.snapshot_valid && view.data_valid;
    changed |= usage_row_window(s_rows[0], valid ? view.snapshot.limit_window_minutes : 0,
                                300, ui_text(UiTextId::UsageFiveHours));
    changed |= usage_row_window(s_rows[1], valid ? view.snapshot.secondary_limit_window_minutes : 0,
                                10080, ui_text(UiTextId::UsageSevenDays));
    changed |= usage_row_quota(s_rows[0], valid, view.snapshot.remaining_percent,
                               view.snapshot.quota_reset_seconds, view.received_tick_ms, now);
    changed |= usage_row_quota(s_rows[1], valid && view.snapshot.secondary_available,
                               view.snapshot.secondary_remaining_percent,
                               view.snapshot.secondary_quota_reset_seconds,
                               view.received_tick_ms, now);
    char paid[32];
    if (valid && view.snapshot.paid_credits_state == CodexPaidCreditsState::Finite) {
        codex_usage_format_credits(view.snapshot.paid_credits_balance, paid, sizeof(paid));
    } else {
        snprintf(paid, sizeof(paid), "%s",
                 valid && view.snapshot.paid_credits_state == CodexPaidCreditsState::Unlimited
                     ? ui_text(UiTextId::CodexUnlimited) : ui_text(UiTextId::UiPlaceholder));
    }
    changed |= usage_row_update(s_rows[2], paid, ui_text(UiTextId::UsageBalance), 0, false);
    changed |= update_bottom(view, now, time_valid);
    return changed;
}

void clear_codex_usage_page_object_refs()
{
    s_online_content = s_offline_content = nullptr;
    for (auto &row : s_rows) row = {};
    s_bottom = {};
}
