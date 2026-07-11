// 构建 60 段工作页进度条，并按填充变化执行局部刷新。
#include "ui_progress.h"

#include "ui_views.h"

#define UI_PROGRESS_CANVAS_BUILD_INVALID_ARG_LOG "progress canvas build invalid arg"
#define UI_PROGRESS_CANVAS_CREATE_FAILED_LOG "progress canvas create failed"

namespace {
constexpr int kProgressSegmentCount = 60;
constexpr int kProgressSegmentW = 5;
constexpr int kProgressSegmentH = 3;
constexpr int kProgressSegmentGap = 1;
constexpr int kProgressSegmentStride = kProgressSegmentW + kProgressSegmentGap;
constexpr int kProgressCanvasX = 20;
constexpr int kProgressCanvasW = kProgressSegmentCount * kProgressSegmentStride - kProgressSegmentGap;
constexpr int kProgressCanvasH = kProgressSegmentH;

bool is_progress_segment_index(int index)
{
    return index >= 0 && index < kProgressSegmentCount;
}

int clamp_progress_filled_count(int filled)
{
    if (filled < 0) {
        return 0;
    }
    if (filled > kProgressSegmentCount) {
        return kProgressSegmentCount;
    }
    return filled;
}

bool is_progress_segment_border_pixel(int x, int y)
{
    return x == 0 || x == kProgressSegmentW - 1 ||
           y == 0 || y == kProgressSegmentH - 1;
}

bool progress_update_args_valid(lv_obj_t *canvas, const int *last_filled)
{
    return canvas && last_filled;
}
}

void draw_progress_segment(lv_obj_t *canvas, int index, bool filled)
{
    if (!canvas || !is_progress_segment_index(index)) {
        return;
    }
    int x0 = index * kProgressSegmentStride;
    for (int y = 0; y < kProgressSegmentH; ++y) {
        for (int x = 0; x < kProgressSegmentW; ++x) {
            bool border = is_progress_segment_border_pixel(x, y);
            lv_canvas_set_px_color(canvas, x0 + x, y, (filled || border) ? lv_color_black() : lv_color_white());
        }
    }
}

void invalidate_progress_segment(lv_obj_t *canvas, int index)
{
    if (!canvas || !is_progress_segment_index(index)) {
        return;
    }
    int x0 = index * kProgressSegmentStride;
    invalidate_canvas_rect(canvas, x0, 0, x0 + kProgressSegmentW - 1, kProgressSegmentH - 1);
}

void build_progress_canvas(lv_obj_t *parent, lv_obj_t **canvas, lv_color_t **buf, int y)
{
    if (!parent || !canvas || !buf) {
        ESP_LOGW(TAG, "%s", UI_PROGRESS_CANVAS_BUILD_INVALID_ARG_LOG);
        return;
    }
    if (!*buf) {
        *buf = alloc_canvas_buffer(kProgressCanvasW, kProgressCanvasH);
    }
    if (!*buf) {
        return;
    }
    *canvas = lv_canvas_create(parent);
    if (!*canvas) {
        ESP_LOGW(TAG, "%s", UI_PROGRESS_CANVAS_CREATE_FAILED_LOG);
        return;
    }
    lv_obj_clear_flag(*canvas, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_box(*canvas, kProgressCanvasX, y, kProgressCanvasW, kProgressCanvasH);
    lv_obj_set_style_border_width(*canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(*canvas, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(*canvas, *buf, kProgressCanvasW, kProgressCanvasH, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(*canvas, lv_color_white(), LV_OPA_COVER);
    for (int i = 0; i < kProgressSegmentCount; ++i) {
        draw_progress_segment(*canvas, i, false);
    }
    lv_obj_invalidate(*canvas);
}

void update_progress_canvas(lv_obj_t *canvas, int filled, int *last_filled)
{
    if (!progress_update_args_valid(canvas, last_filled)) {
        return;
    }
    filled = clamp_progress_filled_count(filled);
    if (*last_filled < 0 || filled < *last_filled) {
        for (int i = 0; i < kProgressSegmentCount; ++i) {
            draw_progress_segment(canvas, i, i < filled);
        }
        lv_obj_invalidate(canvas);
        *last_filled = filled;
        return;
    }
    if (filled == *last_filled) {
        return;
    }
    for (int i = *last_filled; i < filled; ++i) {
        draw_progress_segment(canvas, i, true);
        invalidate_progress_segment(canvas, i);
    }
    *last_filled = filled;
}
