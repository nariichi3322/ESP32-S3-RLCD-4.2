// 声明温湿时钟与小智共用的反显 DSEG 数字牌像素绘制接口。
#pragma once

#include "lvgl.h"

namespace inverted_clock_card {

inline constexpr int kWidth = 112;
inline constexpr int kHeight = 112;

void render(lv_obj_t *canvas, int value);
void clear(lv_obj_t *canvas);

} // namespace inverted_clock_card
