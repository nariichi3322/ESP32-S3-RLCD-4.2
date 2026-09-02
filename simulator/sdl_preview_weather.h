// 提供 SDL 天氣看板主體和預覽共用的供應商中立圖示文字。
#pragma once

#include "lvgl.h"

const char *preview_weather_icon_text(const char *code);
void build_weather_board_preview_body(lv_obj_t *screen);
