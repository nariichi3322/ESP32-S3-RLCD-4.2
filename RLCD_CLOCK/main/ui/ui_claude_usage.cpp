#include "ui_claude_usage.h"

#include "battery_runtime_state.h"
#include "sensor_time.h"
#include "claude_detail_view.h"
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
#include <string.h>
#include <time.h>

namespace {
lv_obj_t *s_online_content;
lv_obj_t *s_offline_content;
UsageRow s_rows[4];
UsageBottom s_bottom;

UiTextId grant_type(ClaudeDetailGrantType type)
{
    switch (type) {
    case ClaudeDetailGrantType::Full: return UiTextId::UsageGrantFull;
    case ClaudeDetailGrantType::Fable: return UiTextId::UsageGrantFable;
    case ClaudeDetailGrantType::Partial: return UiTextId::UsageGrantPartial;
    default: return UiTextId::UsageGrantOther;
    }
}

bool update_fable(const ClaudeDetailView &detail, time_t wall_time)
{
    const char *value = ui_text(UiTextId::UiPlaceholder);
    char value_buf[16];
    char reset[80] = "";
    int percent = 0;
    switch (detail.fable) {
    case ClaudeDetailFableState::NotLoggedIn:
        value = ui_text(UiTextId::UsageNotLoggedIn);
        break;
    case ClaudeDetailFableState::NoPlan:
        value = ui_text(UiTextId::UsageNoPlan);
        break;
    case ClaudeDetailFableState::Value: {
        percent = detail.fable_remaining_percent;
        snprintf(value_buf, sizeof(value_buf), "%u%%", static_cast<unsigned>(percent));
        value = value_buf;
        if (!detail.fable_resets_at || wall_time <= 0) {
            snprintf(reset, sizeof(reset), "%s", ui_text(UiTextId::CodexResetPlaceholder));
        } else {
            const uint64_t current = static_cast<uint64_t>(wall_time);
            const uint64_t remaining = detail.fable_resets_at > current
                                           ? detail.fable_resets_at - current : 0;
            const uint32_t seconds = remaining > UINT32_MAX ? UINT32_MAX
                                                            : static_cast<uint32_t>(remaining);
            char duration[48];
            usage_format_duration(seconds, duration, sizeof(duration));
            snprintf(reset, sizeof(reset), ui_format(UiTextId::CodexResetFormat), duration);
        }
        break;
    }
    default:
        break;
    }
    return usage_row_update(s_rows[2], value, reset, percent);
}

bool update_spend(const ClaudeDetailView &detail)
{
    const char *value = ui_text(UiTextId::UiPlaceholder);
    char value_buf[16];
    char sub[192] = "";
    int percent = 0;
    bool show_meter = true;
    switch (detail.spend) {
    case ClaudeDetailSpendState::NotLoggedIn:
        value = ui_text(UiTextId::UsageNotLoggedIn);
        break;
    case ClaudeDetailSpendState::Disabled:
        value = ui_text(UiTextId::UsageDisabled);
        break;
    case ClaudeDetailSpendState::Value: {
        char used[48];
        char limit[48];
        char used_money[64];
        char limit_money[64];
        usage_format_amount(detail.spend_used, detail.exponent, used, sizeof(used));
        usage_format_amount(detail.spend_limit, detail.exponent, limit, sizeof(limit));
        const bool usd = memcmp(detail.currency, "USD", 3) == 0;
        if (usd) {
            snprintf(used_money, sizeof(used_money), "$%s", used);
            snprintf(limit_money, sizeof(limit_money), "$%s", limit);
        } else {
            snprintf(used_money, sizeof(used_money), "%c%c%c %s",
                     detail.currency[0], detail.currency[1], detail.currency[2], used);
            snprintf(limit_money, sizeof(limit_money), "%s", limit);
        }
        if (detail.spend_limit) {
            percent = static_cast<int>(usage_spent_percent(detail.spend_used, detail.spend_limit));
            snprintf(value_buf, sizeof(value_buf), "%d%%", percent);
            value = value_buf;
            snprintf(sub, sizeof(sub), ui_format(UiTextId::UsageSpentBoth),
                     used_money, limit_money);
        } else {
            value = "";
            show_meter = false;
            snprintf(sub, sizeof(sub), ui_format(UiTextId::UsageSpentOne), used_money);
        }
        break;
    }
    default:
        break;
    }
    return usage_row_update(s_rows[3], value, sub, percent, show_meter);
}

bool update_bottom(const ClaudeDetailView &detail, const CodexUsageSnapshotView &view)
{
    const UsageBottomSection section = usage_bottom_section_load(UsagePage::Claude);
    char title[64];
    snprintf(title, sizeof(title), "%s%s",
             ui_text(section == UsageBottomSection::Stats ? UiTextId::UsageStats : UiTextId::UsageGrants),
             section == UsageBottomSection::Grants && detail.more ? "+" : "");
    char left[2][128]{};
    char right[2][96]{};
    const char *message = nullptr;
    bool hide_title = false;
    switch (detail.grants) {
    case ClaudeDetailGrantsState::NotLoggedIn:
        message = ui_text(UiTextId::UsageClaudeNotLoggedIn);
        hide_title = true;
        break;
    case ClaudeDetailGrantsState::None:
        message = ui_text(UiTextId::UsageNoGrants);
        break;
    case ClaudeDetailGrantsState::Value:
        for (unsigned i = 0; i < detail.grant_count && i < 2; ++i) {
            char count[24];
            char date[32];
            usage_format_uint64(detail.grant_rows[i].resets_left, count, sizeof(count));
            snprintf(left[i], sizeof(left[i]), ui_format(UiTextId::UsageGrantCount),
                     ui_text(grant_type(detail.grant_rows[i].type)), count);
            usage_format_date(detail.grant_rows[i].ends_at, date, sizeof(date));
            if (strcmp(date, ui_text(UiTextId::UiPlaceholder)) == 0) {
                snprintf(right[i], sizeof(right[i]), "%s", date);
            } else {
                snprintf(right[i], sizeof(right[i]), ui_format(UiTextId::UsageExpires), date);
            }
        }
        break;
    default:
        message = ui_text(UiTextId::UiPlaceholder);
        break;
    }
    const UsageStats stats = usage_format_stats(view, false);
    return usage_bottom_update(s_bottom, section, title, left[0], right[0],
                               left[1], right[1], message, hide_title,
                               stats.today, stats.week, stats.running);
}
}

void build_claude_usage_page()
{
    if (work_page_root(kWorkPageClaudeUsage)) return;
    lv_obj_t *screen = create_page_root();
    if (!screen) return;
    set_work_page_root(kWorkPageClaudeUsage, screen);
    build_work_page_battery_icon(screen, kWorkPageClaudeUsage);
    build_work_page_status_bar(screen, kWorkPageClaudeUsage, true, true);
    make_black_bar(screen, 18, 65, 364, 2);
    s_online_content = usage_content_container(screen);
    s_offline_content = usage_content_container(screen);
    if (!s_online_content || !s_offline_content) return;
    const UiTextId titles[] = {UiTextId::UsageFiveHours, UiTextId::UsageSevenDays,
                               UiTextId::UsageFable, UiTextId::UsagePaid};
    const int ys[] = {6, 48, 90, 132};
    for (int i = 0; i < 4; ++i) {
        s_rows[i] = usage_row_create(s_online_content, ys[i], ui_text(titles[i]), true);
    }
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
    build_work_page_day_progress(screen, kWorkPageClaudeUsage);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageClaudeUsage, battery_percent_load());
}

bool update_claude_usage_page(const struct tm &local,
                              const CodexUsageSnapshotView &view,
                              bool feature_enabled)
{
    const bool time_valid = is_tm_plausible(local);
    bool changed = time_valid ? update_work_page_status_time(kWorkPageClaudeUsage, local) : false;
    changed |= set_obj_visible(s_online_content, feature_enabled);
    changed |= set_obj_visible(s_offline_content, !feature_enabled);
    if (!feature_enabled) return changed;

    ClaudeDetailSnapshotView detail_snapshot{};
    const bool copied = claude_detail_snapshot_copy(&detail_snapshot);
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const ClaudeDetailView detail = claude_detail_make_view(
        detail_snapshot.snapshot, copied && detail_snapshot.valid,
        detail_snapshot.received_tick_ms, now);
    const bool valid = view.snapshot_valid && view.data_valid;
    changed |= usage_row_window(s_rows[0], valid ? view.snapshot.limit_window_minutes : 0,
                                300, ui_text(UiTextId::UsageFiveHours));
    changed |= usage_row_window(s_rows[1], valid ? view.snapshot.secondary_limit_window_minutes : 0,
                                10080, ui_text(UiTextId::UsageSevenDays));
    changed |= usage_row_quota(s_rows[0], valid && view.snapshot.limit_window_minutes != 0,
                               view.snapshot.remaining_percent, view.snapshot.quota_reset_seconds,
                               view.received_tick_ms, now);
    changed |= usage_row_quota(s_rows[1], valid && view.snapshot.secondary_available &&
                                              view.snapshot.secondary_limit_window_minutes != 0,
                               view.snapshot.secondary_remaining_percent,
                               view.snapshot.secondary_quota_reset_seconds,
                               view.received_tick_ms, now);
    changed |= update_fable(detail, time_valid ? time(nullptr) : 0);
    changed |= update_spend(detail);
    changed |= update_bottom(detail, view);
    return changed;
}

void clear_claude_usage_page_object_refs()
{
    s_online_content = s_offline_content = nullptr;
    for (auto &row : s_rows) row = {};
    s_bottom = {};
}
