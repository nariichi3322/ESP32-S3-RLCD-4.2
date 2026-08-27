#include "ui_codex_usage.h"

#include "battery_runtime_state.h"
#include "ui_battery.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_widgets.h"
#include "ui_work_status.h"
#include "work_page_ids.h"

#include <stdio.h>
#include <esp_timer.h>

namespace {
lv_obj_t *s_percent;
lv_obj_t *s_reset;
lv_obj_t *s_today;
lv_obj_t *s_week;
lv_obj_t *s_run;
lv_obj_t *s_credits;
lv_obj_t *s_expiry;
lv_obj_t *s_state;

lv_obj_t *metric(lv_obj_t *parent, int x, int y, const char *title)
{
    make_label_with_font(parent, x, y, 92, 18, title, &lv_font_montserrat_12);
    lv_obj_t *value = make_label_with_font(parent, x, y + 19, 92, 29, "--", &lv_font_montserrat_16);
    if (value) lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return value;
}

void format_duration(uint32_t seconds, char *out, size_t size)
{
    const uint32_t minutes = seconds / 60U;
    const uint32_t hours = minutes / 60U;
    const uint32_t days = hours / 24U;
    if (days) snprintf(out, size, "%lud %luh", static_cast<unsigned long>(days), static_cast<unsigned long>(hours % 24U));
    else if (hours) snprintf(out, size, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes % 60U));
    else snprintf(out, size, "%lum", static_cast<unsigned long>(minutes));
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
    make_label_with_font(screen, 18, 76, 170, 20, "CODEX LEFT", &lv_font_montserrat_14);
    s_percent = make_label_with_font(screen, 18, 96, 170, 62, "--", &lv_font_montserrat_48);
    s_reset = make_label_with_font(screen, 18, 160, 170, 25, "RESET --", &lv_font_montserrat_16);
    make_black_bar(screen, 198, 76, 2, 166);
    s_today = metric(screen, 212, 76, "TODAY");
    s_week = metric(screen, 302, 76, "7 DAYS");
    s_run = metric(screen, 212, 132, "RUN");
    s_credits = metric(screen, 302, 132, "CREDITS");
    s_expiry = metric(screen, 212, 188, "EXPIRY");
    s_state = make_centered_label_with_font(screen, 302, 202, 80, 31, "WAITING", &lv_font_montserrat_14, "codex state label create failed");
    if (s_state) {
        lv_obj_set_style_border_width(s_state, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_state, lv_color_black(), LV_PART_MAIN);
    }
    build_work_page_day_progress(screen, kWorkPageCodexUsage);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageCodexUsage, battery_percent_load());
}

bool update_codex_usage_page(const struct tm &, const CodexUsageSnapshotView &view)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const CodexUsageLinkState state = codex_usage_link_state(
        view.data_valid, view.ble_connected, view.last_valid_tick_ms, now);
    bool changed = set_label_text_if_changed(s_state,
        state == CodexUsageLinkState::Waiting ? "WAITING" :
        state == CodexUsageLinkState::Linked ? "LINKED" : "STALE");
    if (!view.data_valid) {
        changed |= set_label_text_if_changed(s_percent, "--");
        changed |= set_label_text_if_changed(s_reset, "RESET --");
        changed |= set_label_text_if_changed(s_today, "--");
        changed |= set_label_text_if_changed(s_week, "--");
        changed |= set_label_text_if_changed(s_run, "--");
        changed |= set_label_text_if_changed(s_credits, "--");
        changed |= set_label_text_if_changed(s_expiry, "--");
        return changed;
    }
    char text[32];
    snprintf(text, sizeof(text), "%u%%", view.snapshot.remaining_percent);
    changed |= set_label_text_if_changed(s_percent, text);
    format_duration(codex_usage_countdown_seconds(view.snapshot.quota_reset_seconds, view.received_tick_ms, now), text, sizeof(text));
    char reset[40]; snprintf(reset, sizeof(reset), "RESET %s", text);
    changed |= set_label_text_if_changed(s_reset, reset);
    char token[24]; codex_usage_format_tokens(view.snapshot.tokens_today, token, sizeof(token));
    snprintf(text, sizeof(text), "%s%s", view.snapshot.tokens_today_estimated ? "~" : "", token);
    changed |= set_label_text_if_changed(s_today, text);
    codex_usage_format_tokens(view.snapshot.tokens_7d, token, sizeof(token));
    changed |= set_label_text_if_changed(s_week, token);
    snprintf(text, sizeof(text), "%u", view.snapshot.active_threads);
    changed |= set_label_text_if_changed(s_run, text);
    snprintf(text, sizeof(text), "%u", view.snapshot.reset_credits);
    changed |= set_label_text_if_changed(s_credits, text);
    format_duration(codex_usage_countdown_seconds(view.snapshot.next_credit_expiry_seconds, view.received_tick_ms, now), text, sizeof(text));
    changed |= set_label_text_if_changed(s_expiry, text);
    return changed;
}

void clear_codex_usage_page_object_refs()
{
    s_percent = s_reset = s_today = s_week = s_run = s_credits = s_expiry = s_state = nullptr;
}
