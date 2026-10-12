#include "ui_claude_usage.h"

#include "battery_runtime_state.h"
#include "ui_battery.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
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
lv_obj_t *s_primary_title;
lv_obj_t *s_percent;
lv_obj_t *s_reset;
lv_obj_t *s_secondary_title;
lv_obj_t *s_secondary_percent;
lv_obj_t *s_secondary_reset;
lv_obj_t *s_today;
lv_obj_t *s_week;
lv_obj_t *s_run;

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
    make_label_with_font(parent, x, y, width, 18, title, ui_font(UiFontRole::Body16));
    lv_obj_t *value = make_label_with_font(parent, x, y + 19, width, 29, ui_text(UiTextId::UiPlaceholder), &lv_font_montserrat_16);
    if (value) lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return value;
}

void format_duration(uint32_t seconds, char *out, size_t size)
{
    const uint32_t minutes = seconds / 60U;
    const uint32_t hours = minutes / 60U;
    const uint32_t days = hours / 24U;
    if (days) {
        snprintf(out, size, ui_format(UiTextId::CodexDurationDaysHours),
                 static_cast<unsigned long>(days),
                 static_cast<unsigned long>(hours % 24U));
    } else if (hours) {
        snprintf(out, size, ui_format(UiTextId::CodexDurationHoursMinutes),
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes % 60U));
    } else {
        snprintf(out, size, ui_format(UiTextId::CodexDurationMinutes),
                 static_cast<unsigned long>(minutes));
    }
}

lv_obj_t *quota_block(lv_obj_t *parent, int y, lv_obj_t **percent, lv_obj_t **reset)
{
    lv_obj_t *title = make_label_with_font(parent, 18, y, 170, 18,
                                           ui_text(UiTextId::ClaudePrimaryTitlePlaceholder),
                                           ui_font(UiFontRole::Body16));
    *percent = make_label_with_font(parent, 18, y + 18, 170, 35,
                                    ui_text(UiTextId::UiPlaceholder), &lv_font_montserrat_32);
    *reset = make_label_with_font(parent, 18, y + 54, 170, 18,
                                  ui_text(UiTextId::CodexResetPlaceholder),
                                  ui_font(UiFontRole::Body16));
    return title;
}

// window_minutes == 0 marks a window Claude Code has not reported.
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
    snprintf(text, sizeof(text), ui_format(UiTextId::ClaudeQuotaTitle), window);
    bool changed = set_label_text_if_changed(title, text);
    if (!available || window_minutes == 0) {
        changed |= set_label_text_if_changed(percent, ui_text(UiTextId::UiPlaceholder));
        changed |= set_label_text_if_changed(reset, ui_text(UiTextId::CodexResetPlaceholder));
        return changed;
    }
    snprintf(text, sizeof(text), "%u%%", remaining_percent);
    changed |= set_label_text_if_changed(percent, text);
    format_duration(codex_usage_countdown_seconds(
        reset_seconds, received_tick_ms, now), text, sizeof(text));
    char reset_text[40];
    snprintf(reset_text, sizeof(reset_text), ui_format(UiTextId::CodexResetFormat), text);
    changed |= set_label_text_if_changed(reset, reset_text);
    return changed;
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
    s_online_content = make_content_container(screen);
    s_offline_content = make_content_container(screen);
    if (!s_online_content || !s_offline_content) return;
    s_primary_title = quota_block(s_online_content, 6, &s_percent, &s_reset);
    make_black_bar(s_online_content, 18, 84, 170, 2);
    s_secondary_title = quota_block(s_online_content, 96, &s_secondary_percent,
                                    &s_secondary_reset);
    make_black_bar(s_online_content, 198, 9, 2, 174);
    s_today = metric(s_online_content, 212, 9, ui_text(UiTextId::CodexToday));
    s_week = metric(s_online_content, 302, 9, ui_text(UiTextId::CodexSevenDays));
    s_run = metric(s_online_content, 212, 65, ui_text(UiTextId::CodexRunning), 182);
    lv_obj_t *offline_title = make_label_with_font(
        s_offline_content, 25, 56, 350, 34,
        ui_text(UiTextId::CodexBluetoothOff), ui_font(UiFontRole::Body16));
    lv_obj_t *offline_detail = make_label_with_font(
        s_offline_content, 25, 96, 350, 24,
        ui_text(UiTextId::CodexOffline), ui_font(UiFontRole::Body16));
    if (offline_title) {
        lv_obj_set_style_text_align(offline_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    if (offline_detail) {
        lv_obj_set_style_text_align(offline_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
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
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    bool changed = false;
    changed |= update_work_page_status_time(kWorkPageClaudeUsage, local);
    changed |= set_obj_visible(s_online_content, feature_enabled);
    changed |= set_obj_visible(s_offline_content, !feature_enabled);
    if (!feature_enabled) return changed;
    const bool valid = view.snapshot_valid && view.data_valid;
    changed |= update_quota_block(
        s_primary_title, s_percent, s_reset, valid,
        view.snapshot.remaining_percent, view.snapshot.limit_window_minutes,
        view.snapshot.quota_reset_seconds, view.received_tick_ms, now);
    changed |= update_quota_block(
        s_secondary_title, s_secondary_percent, s_secondary_reset,
        valid && view.snapshot.secondary_available,
        view.snapshot.secondary_remaining_percent,
        view.snapshot.secondary_limit_window_minutes,
        view.snapshot.secondary_quota_reset_seconds,
        view.received_tick_ms, now);
    if (!valid) {
        changed |= set_label_text_if_changed(s_today, ui_text(UiTextId::UiPlaceholder));
        changed |= set_label_text_if_changed(s_week, ui_text(UiTextId::UiPlaceholder));
        changed |= set_label_text_if_changed(s_run, ui_text(UiTextId::UiPlaceholder));
        return changed;
    }
    char text[24];
    codex_usage_format_tokens(view.snapshot.tokens_today, text, sizeof(text));
    changed |= set_label_text_if_changed(s_today, text);
    codex_usage_format_tokens(view.snapshot.tokens_7d, text, sizeof(text));
    changed |= set_label_text_if_changed(s_week, text);
    snprintf(text, sizeof(text), "%u", view.snapshot.active_threads);
    changed |= set_label_text_if_changed(s_run, text);
    return changed;
}

void clear_claude_usage_page_object_refs()
{
    s_online_content = s_offline_content = nullptr;
    s_primary_title = s_percent = s_reset = s_secondary_title =
        s_secondary_percent = s_secondary_reset = s_today = s_week = s_run =
        nullptr;
}
