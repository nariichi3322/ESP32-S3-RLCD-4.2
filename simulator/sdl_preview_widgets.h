// 提供 SDL 预览页面共用的基础 LVGL 标签和黑白条控件。
#pragma once

#include "lvgl.h"

namespace sdl_preview_widgets {

void set_obj_black(lv_obj_t *obj, bool active);
lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *make_label_with_font(lv_obj_t *parent,
                               int x,
                               int y,
                               int w,
                               int h,
                               const char *text,
                               const lv_font_t *font);
lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h, const char *text);

} // namespace sdl_preview_widgets
