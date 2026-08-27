// 构建 Codex Usage 六种验收状态的 SDL 静态预览。
#pragma once

#include <lvgl.h>
#include <stdint.h>

struct CodexPreviewFixture {
    const char *state;
    bool data_valid;
    uint8_t remaining_percent;
    const char *reset;
    const char *today;
    const char *week;
    uint16_t active_threads;
    uint16_t reset_credits;
    const char *expiry;
};

CodexPreviewFixture codex_preview_fixture(const char *mode);
void build_codex_preview_body(lv_obj_t *screen, const char *mode);
