// 绘制反显 DSEG 数字牌的黑底、缩放数字和圆角裁切像素。
#include "ui_inverted_clock_card.h"

#include "ui_views.h"
#include "ui_dseg_layout.h"

namespace inverted_clock_card {
namespace {

constexpr int kRadius = 8;
constexpr int kDigitScaleNumerator = 3;
constexpr int kDigitScaleDenominator = 4;
constexpr int kDigitBaselineY = 84;

static_assert(kRadius > 0 && kRadius * 2 <= kWidth && kRadius * 2 <= kHeight,
              "inverted clock card radius must fit card");
static_assert(kDigitScaleNumerator > 0 && kDigitScaleDenominator > 0,
              "inverted clock digit scale must be positive");

void apply_rounding(lv_obj_t *canvas)
{
    if (!canvas) {
        return;
    }
    int r2 = kRadius * kRadius;
    for (int y = 0; y < kRadius; ++y) {
        for (int x = 0; x < kRadius; ++x) {
            int dx = kRadius - 1 - x;
            int dy = kRadius - 1 - y;
            if (dx * dx + dy * dy > r2) {
                canvas_set_px_safe(canvas, x, y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas, kWidth - 1 - x, y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas, x, kHeight - 1 - y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas,
                                   kWidth - 1 - x,
                                   kHeight - 1 - y,
                                   kWidth,
                                   kHeight,
                                   lv_color_white());
            }
        }
    }
}

bool dseg_pixel_on(const DsegFont &font, const DsegGlyph *glyph, int x, int y)
{
    uint32_t bit = static_cast<uint32_t>(y) * glyph->width + x;
    return packed_1bit_bit_is_set(font.bitmap + glyph->bitmap_offset, bit);
}

void draw_scaled_dseg_digit(lv_obj_t *canvas,
                            const DsegGlyph *glyph,
                            int origin_x,
                            int origin_y,
                            int scale_num,
                            int scale_den,
                            int clip_y0 = 0,
                            int clip_y1 = kHeight)
{
    if (!canvas || !glyph || scale_num <= 0 || scale_den <= 0) {
        return;
    }
    int dst_w = (glyph->width * scale_num + scale_den - 1) / scale_den;
    int dst_h = (glyph->height * scale_num + scale_den - 1) / scale_den;
    int dst_x = origin_x + (glyph->x_offset * scale_num) / scale_den;
    int dst_y = origin_y + (glyph->y_offset * scale_num) / scale_den;
    for (int y = 0; y < dst_h; ++y) {
        int src_y = (y * glyph->height) / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            int src_x = (x * glyph->width) / dst_w;
            int py = dst_y + y;
            if (py >= clip_y0 && py < clip_y1 &&
                dseg_pixel_on(kDSEG84Font, glyph, src_x, src_y)) {
                canvas_set_px_safe(canvas,
                                   dst_x + x,
                                   dst_y + y,
                                   kWidth,
                                   kHeight,
                                   lv_color_white());
            }
        }
    }
}

void draw_digits(lv_obj_t *canvas, int value, int clip_y0 = 0, int clip_y1 = kHeight)
{
    const DsegGlyph *tens = find_dseg_glyph(kDSEG84Font, static_cast<char>('0' + value / 10));
    const DsegGlyph *ones = find_dseg_glyph(kDSEG84Font, static_cast<char>('0' + value % 10));
    if (!tens || !ones) {
        return;
    }
    DsegPairLayout layout = centered_dseg_pair_layout(kWidth,
                                                       kDigitScaleNumerator,
                                                       kDigitScaleDenominator,
                                                       tens->x_offset,
                                                       tens->width,
                                                       tens->x_advance,
                                                       ones->x_offset,
                                                       ones->width);
    draw_scaled_dseg_digit(canvas,
                           tens,
                           layout.first_origin_x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
    draw_scaled_dseg_digit(canvas,
                           ones,
                           layout.second_origin_x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
}

void draw_shell(lv_obj_t *canvas)
{
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
}

} // namespace

void render(lv_obj_t *canvas, int value)
{
    if (!canvas) {
        return;
    }
    draw_shell(canvas);
    draw_digits(canvas, value);
    apply_rounding(canvas);
    lv_obj_invalidate(canvas);
}

void clear(lv_obj_t *canvas)
{
    if (!canvas) {
        return;
    }
    draw_shell(canvas);
    apply_rounding(canvas);
    lv_obj_invalidate(canvas);
}

} // namespace inverted_clock_card
