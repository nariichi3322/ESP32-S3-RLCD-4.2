// 声明关于本机页面请求与保持截止 Tick 的一致快照接口。
#pragma once

#include <stdint.h>

struct InfoPageStateSnapshot {
    bool requested = false;
    uint32_t hold_until_tick = 0;
    uint32_t revision = 0;
};

bool info_page_state_init();
void info_page_state_load(InfoPageStateSnapshot *out);
bool info_page_requested();
void info_page_request(uint32_t hold_until_tick);
void info_page_clear();
bool info_page_clear_if_current(const InfoPageStateSnapshot &expected);
