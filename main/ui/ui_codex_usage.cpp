#include "ui_codex_usage.h"

#include "battery_runtime_state.h"
#include "codex_usage_feature_state.h"
#include "ui_battery.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_widgets.h"
#include "ui_work_status.h"
#include "work_page_ids.h"

#include <stdio.h>
#include <esp_timer.h>

namespace {
lv_obj_t *s_online_content;
lv_obj_t *s_offline_content;
lv_obj_t *s_percent;
lv_obj_t *s_reset;
lv_obj_t *s_primary_title;
lv_obj_t *s_secondary_title;
lv_obj_t *s_secondary_percent;
lv_obj_t *s_secondary_reset;
lv_obj_t *s_today;
lv_obj_t *s_week;
lv_obj_t *s_run;
lv_obj_t *s_paid;
lv_obj_t *s_reset_credits;
lv_obj_t *s_reset_expiry;

lv_obj_t *make_content_container(lv_obj_t *screen)
{
    lv_obj_t *content = lv_obj_create(screen);
    if (!content) return nullptr;
    lv_obj_set_pos(content, 0, 67);
    lv_obj_set_size(content, 400, 183);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    return content;
}

lv_obj_t *metric(lv_obj_t *parent, int x, int y, const char *title, int width = 92)
{
    make_label_with_font(parent, x, y, width, 18, title, &lv_font_montserrat_12);
    lv_obj_t *value = make_label_with_font(parent, x, y + 19, width, 29, "--", &lv_font_montserrat_16);
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
    s_online_content = make_content_container(screen);
    s_offline_content = make_content_container(screen);
    if (!s_online_content || !s_offline_content) return;
    s_primary_title = make_label_with_font(s_online_content, 18, 6, 170, 18, "CODEX LEFT (--)", &lv_font_montserrat_12);
    s_percent = make_label_with_font(s_online_content, 18, 24, 170, 32, "--", &lv_font_montserrat_24);
    s_reset = make_label_with_font(s_online_content, 18, 54, 170, 20, "RESET --", &lv_font_montserrat_12);
    make_black_bar(s_online_content, 18, 79, 170, 2);
    s_secondary_title = make_label_with_font(s_online_content, 18, 85, 170, 18, "CODEX LEFT (--)", &lv_font_montserrat_12);
    s_secondary_percent = make_label_with_font(s_online_content, 18, 103, 170, 32, "--", &lv_font_montserrat_24);
    s_secondary_reset = make_label_with_font(s_online_content, 18, 133, 170, 20, "RESET --", &lv_font_montserrat_12);
    make_black_bar(s_online_content, 18, 157, 170, 2);
    make_label_with_font(s_online_content, 18, 162, 108, 18, "PAID CREDITS", &lv_font_montserrat_12);
    s_paid = make_label_with_font(s_online_content, 126, 159, 62, 24, "--", &lv_font_montserrat_16);
    if (s_paid) lv_obj_set_style_text_align(s_paid, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    make_black_bar(s_online_content, 198, 9, 2, 174);
    s_today = metric(s_online_content, 212, 9, "TODAY");
    s_week = metric(s_online_content, 302, 9, "7 DAYS");
    s_run = metric(s_online_content, 212, 65, "RUN", 182);
    s_reset_credits = metric(s_online_content, 212, 121, "RESET CR");
    s_reset_expiry = metric(s_online_content, 302, 121, "RESET EXP");
    lv_obj_t *offline_title = make_label_with_font(
        s_offline_content, 25, 56, 350, 34, "BLUETOOTH OFF", &lv_font_montserrat_24);
    lv_obj_t *offline_detail = make_label_with_font(
        s_offline_content, 25, 96, 350, 24, "(OFFLINE)", &lv_font_montserrat_16);
    if (offline_title) {
        lv_obj_set_style_text_align(offline_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    if (offline_detail) {
        lv_obj_set_style_text_align(offline_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    const bool feature_enabled = codex_usage_feature_enabled();
    set_obj_visible(s_online_content, feature_enabled);
    set_obj_visible(s_offline_content, !feature_enabled);
    build_work_page_day_progress(screen, kWorkPageCodexUsage);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageCodexUsage, battery_percent_load());
}

bool update_codex_usage_page(const struct tm &local,
                             const CodexUsageSnapshotView &view,
                             bool feature_enabled)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    bool changed = false;
    changed |= update_work_page_status_time(kWorkPageCodexUsage, local);
    changed |= set_obj_visible(s_online_content, feature_enabled);
    changed |= set_obj_visible(s_offline_content, !feature_enabled);
    if (!feature_enabled) return changed;
    if (!view.snapshot_valid) {
        changed |= update_quota_block(s_primary_title, s_percent, s_reset,
                                      false, 0, 0, 0, 0, now);
        changed |= update_quota_block(s_secondary_title, s_secondary_percent,
                                      s_secondary_reset, false, 0, 0, 0, 0, now);
        changed |= set_label_text_if_changed(s_today, "--");
        changed |= set_label_text_if_changed(s_week, "--");
        changed |= set_label_text_if_changed(s_run, "--");
        changed |= set_label_text_if_changed(s_paid, "--");
        changed |= set_label_text_if_changed(s_reset_credits, "--");
        changed |= set_label_text_if_changed(s_reset_expiry, "--");
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
    if (view.snapshot.paid_credits_state == CodexPaidCreditsState::Unlimited) {
        changed |= set_label_text_if_changed(s_paid, "UNLIM");
    } else if (view.snapshot.paid_credits_state == CodexPaidCreditsState::Finite) {
        codex_usage_format_credits(view.snapshot.paid_credits_balance, text, sizeof(text));
        changed |= set_label_text_if_changed(s_paid, text);
    } else {
        changed |= set_label_text_if_changed(s_paid, "--");
    }
    snprintf(text, sizeof(text), "%u", view.snapshot.reset_credits);
    changed |= set_label_text_if_changed(s_reset_credits, text);
    const uint32_t expiry = codex_usage_countdown_seconds(
        view.snapshot.next_credit_expiry_seconds, view.received_tick_ms, now);
    if (view.snapshot.next_credit_expiry_seconds == 0 || expiry == 0) {
        changed |= set_label_text_if_changed(s_reset_expiry, "--");
    } else {
        format_duration(expiry, text, sizeof(text));
        changed |= set_label_text_if_changed(s_reset_expiry, text);
    }
    return changed;
}

void clear_codex_usage_page_object_refs()
{
    s_online_content = s_offline_content = nullptr;
    s_primary_title = s_percent = s_reset = s_secondary_title =
        s_secondary_percent = s_secondary_reset = s_today = s_week = s_run =
        s_paid = s_reset_credits = s_reset_expiry = nullptr;
}
