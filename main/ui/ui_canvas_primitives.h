// 声明页面 canvas 复用的坐标钳制、线段、虚线与实心圆绘制图元。
#pragma once

#include "lvgl.h"

int clamp_int(int value, int min_value, int max_value);
void canvas_draw_line(lv_obj_t *canvas,
                      int width,
                      int height,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      lv_color_t color);
void canvas_draw_dashed_hline(lv_obj_t *canvas,
                              int width,
                              int height,
                              int x1,
                              int x2,
                              int y,
                              lv_color_t color);
void canvas_draw_filled_circle(lv_obj_t *canvas,
                               int width,
                               int height,
                               int cx,
                               int cy,
                               int radius,
                               lv_color_t color);
