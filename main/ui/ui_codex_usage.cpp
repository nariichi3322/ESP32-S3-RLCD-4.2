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
lv_obj_t *s_primary_title;
lv_obj_t *s_secondary_title;
lv_obj_t *s_secondary_percent;
lv_obj_t *s_secondary_reset;
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

bool update_quota_block(lv_obj_t *title,
                        lv_obj_t *percent,
                        lv_obj_t *reset,
                        bool available,
                        uint8_t remaining_percent,
                        uint32_t window_minutes,
                        uint32_t reset_seconds,
                        uint32_t received_tick_ms,
                        uint32_t now)
{
    char text[32];
    char window[16];
    codex_usage_format_window(window_minutes, window, sizeof(window));
    snprintf(text, sizeof(text), "CODEX LEFT (%s)", window);
    bool changed = set_label_text_if_changed(title, text);
    if (!available) {
        changed |= set_label_text_if_changed(percent, "--");
        changed |= set_label_text_if_changed(reset, "RESET --");
        return changed;
    }
    snprintf(text, sizeof(text), "%u%%", remaining_percent);
    changed |= set_label_text_if_changed(percent, text);
    format_duration(codex_usage_countdown_seconds(
        reset_seconds, received_tick_ms, now), text, sizeof(text));
    char reset_text[40];
    snprintf(reset_text, sizeof(reset_text), "RESET %s", text);
    changed |= set_label_text_if_changed(reset, reset_text);
    return changed;
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
    s_primary_title = make_label_with_font(screen, 18, 73, 170, 18, "CODEX LEFT (--)", &lv_font_montserrat_12);
    s_percent = make_label_with_font(screen, 18, 91, 170, 32, "--", &lv_font_montserrat_24);
    s_reset = make_label_with_font(screen, 18, 121, 170, 20, "RESET --", &lv_font_montserrat_12);
    make_black_bar(screen, 18, 146, 170, 2);
    s_secondary_title = make_label_with_font(screen, 18, 152, 170, 18, "CODEX LEFT (--)", &lv_font_montserrat_12);
    s_secondary_percent = make_label_with_font(screen, 18, 170, 170, 32, "--", &lv_font_montserrat_24);
    s_secondary_reset = make_label_with_font(screen, 18, 200, 170, 20, "RESET --", &lv_font_montserrat_12);
    make_black_bar(screen, 198, 76, 2, 166);
    s_today = metric(screen, 212, 76, "TODAY");
    s_week = metric(screen, 302, 76, "7 DAYS");
    s_run = metric(screen, 212, 132, "RUN");
    s_credits = metric(screen, 302, 132, "CREDITS");
    s_expiry = metric(screen, 212, 188, "EXPIRY");
    s_state = make_centered_label_with_font(screen, 290, 202, 92, 31, "DISCONNECT", &lv_font_montserrat_12, "codex state label create failed");
    if (s_state) {
        lv_obj_set_style_border_width(s_state, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_state, lv_color_black(), LV_PART_MAIN);
    }
    build_work_page_day_progress(screen, kWorkPageCodexUsage);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageCodexUsage, battery_percent_load());
}

bool update_codex_usage_page(const struct tm &local, const CodexUsageSnapshotView &view)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const CodexUsageLinkState state = codex_usage_link_state(
        view.data_valid, view.ble_connected, view.bonded,
        view.last_valid_tick_ms, now);
    bool changed = set_label_text_if_changed(s_state,
        state == CodexUsageLinkState::Disconnected ? "DISCONNECT" :
        state == CodexUsageLinkState::Waiting ? "WAITING" :
        state == CodexUsageLinkState::Linked ? "LINKED" : "STALE");
    changed |= update_work_page_status_time(kWorkPageCodexUsage, local);
    if (!view.snapshot_valid) {
        changed |= update_quota_block(s_primary_title, s_percent, s_reset,
                                      false, 0, 0, 0, 0, now);
        changed |= update_quota_block(s_secondary_title, s_secondary_percent,
                                      s_secondary_reset, false, 0, 0, 0, 0, now);
        changed |= set_label_text_if_changed(s_today, "--");
        changed |= set_label_text_if_changed(s_week, "--");
        changed |= set_label_text_if_changed(s_run, "--");
        changed |= set_label_text_if_changed(s_credits, "--");
        changed |= set_label_text_if_changed(s_expiry, "--");
        return changed;
    }
    changed |= update_quota_block(
        s_primary_title, s_percent, s_reset, true,
        view.snapshot.remaining_percent, view.snapshot.limit_window_minutes,
        view.snapshot.quota_reset_seconds, view.received_tick_ms, now);
    changed |= update_quota_block(
        s_secondary_title, s_secondary_percent, s_secondary_reset,
        view.snapshot.secondary_available,
        view.snapshot.secondary_remaining_percent,
        view.snapshot.secondary_limit_window_minutes,
        view.snapshot.secondary_quota_reset_seconds,
        view.received_tick_ms, now);
    char text[32];
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
    s_primary_title = s_percent = s_reset = s_secondary_title =
        s_secondary_percent = s_secondary_reset = s_today = s_week = s_run =
        s_credits = s_expiry = s_state = nullptr;
}
