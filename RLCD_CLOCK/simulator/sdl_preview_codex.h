// 建立 Codex Usage 多種驗收狀態的 SDL 靜態預覽。
#pragma once

#include <lvgl.h>
#include <stdint.h>

struct CodexPreviewFixture {
    const char *state;
    bool data_valid;
    uint8_t remaining_percent;
    const char *primary_window;
    const char *reset;
    bool secondary_available;
    uint8_t secondary_remaining_percent;
    const char *secondary_window;
    const char *secondary_reset;
    const char *today;
    const char *week;
    uint16_t active_threads;
    const char *paid;
    uint16_t reset_credits;
    const char *reset_expiry;
};

CodexPreviewFixture codex_preview_fixture(const char *mode);
void build_codex_preview_body(lv_obj_t *screen, const char *mode);
