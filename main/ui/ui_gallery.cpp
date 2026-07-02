// 构建和刷新图片时钟页面的图库、大号时间和每日文字。
#include "ui_views.h"

#include "clock_gallery_images.h"
#include "custom_assets.h"

#define GALLERY_IMAGE_CANVAS_CREATE_FAILED_LOG "gallery image canvas create failed"
#define GALLERY_TIME_CANVAS_CREATE_FAILED_LOG "gallery time canvas create failed"
#define GALLERY_SAYING_LABEL_CREATE_FAILED_LOG "gallery saying label create failed"

static int s_last_gallery_image_index = -1;
static int s_last_gallery_time_key = -1;
static uint8_t s_custom_gallery_image[CLOCK_GALLERY_IMAGE_BYTES_PER_ROW * CLOCK_GALLERY_IMAGE_HEIGHT];

static constexpr int kGalleryTimeCanvasW = 112;
static constexpr int kGalleryTimeCanvasH = 198;
static constexpr int kGalleryMinutesPerHour = 60;
static constexpr int kGalleryBlockDigitCount = 10;
static constexpr int kGalleryBlockDigitRows = 7;
static constexpr int kGalleryBlockDigitCols = 5;
static constexpr int kGalleryBlockDigitScale = 10;
static constexpr int kGalleryBlockDigitGap = 8;
static constexpr int kGalleryBlockDigitW = kGalleryBlockDigitCols * kGalleryBlockDigitScale;
static constexpr int kGalleryBlockDigitH = kGalleryBlockDigitRows * kGalleryBlockDigitScale;
static constexpr int kGalleryBlockNumberW = kGalleryBlockDigitW * 2 + kGalleryBlockDigitGap;
static constexpr int kGalleryTimeHourY = 15;
static constexpr int kGalleryTimeMinuteY = 116;

static const char *const kBlockDigits[kGalleryBlockDigitCount][kGalleryBlockDigitRows] = {
    {"11111", "10001", "10011", "10101", "11001", "10001", "11111"},
    {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
    {"11110", "00001", "00001", "11110", "10000", "10000", "11111"},
    {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
    {"10010", "10010", "10010", "11111", "00010", "00010", "00010"},
    {"11111", "10000", "10000", "11110", "00001", "00001", "11110"},
    {"01111", "10000", "10000", "11110", "10001", "10001", "01110"},
    {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
    {"01110", "10001", "10001", "01110", "10001", "10001", "01110"},
    {"01110", "10001", "10001", "01111", "00001", "00001", "11110"},
};

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

static_assert(array_count(kBlockDigits) == kGalleryBlockDigitCount);
static_assert(kGalleryTimeCanvasW > 0 && kGalleryTimeCanvasH > 0, "Gallery time canvas dimensions must be positive");
static_assert(kGalleryMinutesPerHour > 0, "Gallery minutes per hour must be positive");
static_assert(kGalleryBlockDigitCount == 10, "Gallery block digit table must contain decimal digits");
static_assert(kGalleryBlockDigitRows > 0 && kGalleryBlockDigitCols > 0, "Gallery block digit grid must be positive");
static_assert(kGalleryBlockDigitScale > 1, "Gallery block digit scale must leave a visible gap");
static_assert(kGalleryBlockDigitGap >= 0, "Gallery block digit gap must not be negative");
static_assert(kGalleryBlockNumberW <= kGalleryTimeCanvasW, "Gallery block number must fit the time canvas width");
static_assert(kGalleryTimeHourY >= 0, "Gallery hour Y must not be negative");
static_assert(kGalleryTimeMinuteY >= 0, "Gallery minute Y must not be negative");
static_assert(kGalleryTimeHourY + kGalleryBlockDigitH <= kGalleryTimeCanvasH,
              "Gallery hour digits must fit the time canvas height");
static_assert(kGalleryTimeMinuteY + kGalleryBlockDigitH <= kGalleryTimeCanvasH,
              "Gallery minute digits must fit the time canvas height");

static void canvas_fill_rect(lv_obj_t *canvas, int x, int y, int w, int h, lv_color_t color)
{
    if (!canvas || w <= 0 || h <= 0) {
        return;
    }
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            canvas_set_px_safe(canvas, xx, yy, kGalleryTimeCanvasW, kGalleryTimeCanvasH, color);
        }
    }
}

static void draw_block_digit(lv_obj_t *canvas, int digit, int x, int y, int scale)
{
    if (digit < 0 || digit >= kGalleryBlockDigitCount) {
        return;
    }
    for (int row = 0; row < kGalleryBlockDigitRows; ++row) {
        for (int col = 0; col < kGalleryBlockDigitCols; ++col) {
            if (kBlockDigits[digit][row][col] == '1') {
                canvas_fill_rect(canvas, x + col * scale, y + row * scale, scale - 1, scale - 1, lv_color_black());
            }
        }
    }
}

static void draw_block_number(lv_obj_t *canvas, int value, int y)
{
    int x = (kGalleryTimeCanvasW - kGalleryBlockNumberW) / 2;
    draw_block_digit(canvas, value / 10, x, y, kGalleryBlockDigitScale);
    draw_block_digit(canvas, value % 10, x + kGalleryBlockDigitW + kGalleryBlockDigitGap, y, kGalleryBlockDigitScale);
}

static bool update_gallery_time_labels(const struct tm &local)
{
    if (!g_gallery_time_canvas) {
        return false;
    }
    lv_canvas_fill_bg(g_gallery_time_canvas, lv_color_white(), LV_OPA_COVER);
    draw_block_number(g_gallery_time_canvas, local.tm_hour, kGalleryTimeHourY);
    draw_block_number(g_gallery_time_canvas, local.tm_min, kGalleryTimeMinuteY);
    lv_obj_invalidate(g_gallery_time_canvas);
    return true;
}

static bool update_gallery_saying_label()
{
    if (!g_gallery_saying_label) {
        return false;
    }
    const char *text = g_daily_saying[0] ? g_daily_saying : "";
    return set_label_text_if_changed(g_gallery_saying_label, text);
}

static bool update_gallery_image_for_hour(int hour)
{
    if (!g_gallery_image_canvas) {
        return false;
    }
    int custom_count = custom_assets_gallery_count();
    int image_count = custom_count > 0 ? custom_count : CLOCK_GALLERY_IMAGE_COUNT;
    if (image_count <= 0) {
        return false;
    }
    int image_index = hour % image_count;
    if (image_index == s_last_gallery_image_index) {
        return false;
    }
    s_last_gallery_image_index = image_index;
    const uint8_t *image_bits = clock_gallery_images[image_index % CLOCK_GALLERY_IMAGE_COUNT];
    if (custom_count > 0 &&
        custom_assets_read_gallery_image(image_index, s_custom_gallery_image, sizeof(s_custom_gallery_image))) {
        image_bits = s_custom_gallery_image;
    }
    draw_1bit_icon(g_gallery_image_canvas,
                   CLOCK_GALLERY_IMAGE_WIDTH,
                   CLOCK_GALLERY_IMAGE_HEIGHT,
                   CLOCK_GALLERY_IMAGE_BYTES_PER_ROW,
                   image_bits,
                   lv_color_black(),
                   lv_color_white());
    return true;
}

bool update_gallery_page(const struct tm &local)
{
    build_gallery_page();
    bool changed = false;
    int time_key = local.tm_hour * kGalleryMinutesPerHour + local.tm_min;
    if (time_key != s_last_gallery_time_key) {
        s_last_gallery_time_key = time_key;
        changed |= update_gallery_time_labels(local);
    }
    changed |= update_gallery_image_for_hour(local.tm_hour);
    changed |= update_work_page_sensor_summary(g_gallery_summary_label);
    changed |= update_gallery_saying_label();
    return changed;
}

void build_gallery_page()
{
    if (g_gallery_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_gallery_root = screen;

    build_battery_icon(screen, g_gallery_battery_segments);
    build_work_page_status_bar(screen,
                               kWorkPageGallery,
                               &g_gallery_date_label,
                               &g_gallery_summary_label,
                               &g_gallery_status_time_label,
                               false);

    lv_obj_t *top_line = make_bar(screen, 18, 54, 364, 4);
    set_obj_black(top_line, true);

    if (!g_gallery_image_canvas_buf) {
        g_gallery_image_canvas_buf = alloc_canvas_buffer(CLOCK_GALLERY_IMAGE_WIDTH, CLOCK_GALLERY_IMAGE_HEIGHT);
    }
    g_gallery_image_canvas = lv_canvas_create(screen);
    if (!g_gallery_image_canvas) {
        ESP_LOGW(TAG, "%s", GALLERY_IMAGE_CANVAS_CREATE_FAILED_LOG);
    } else {
        lv_obj_clear_flag(g_gallery_image_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(g_gallery_image_canvas, 20, 62);
        lv_obj_set_size(g_gallery_image_canvas, CLOCK_GALLERY_IMAGE_WIDTH, CLOCK_GALLERY_IMAGE_HEIGHT);
        lv_obj_set_style_border_width(g_gallery_image_canvas, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_gallery_image_canvas, 0, LV_PART_MAIN);
        if (g_gallery_image_canvas_buf) {
            lv_canvas_set_buffer(g_gallery_image_canvas,
                                 g_gallery_image_canvas_buf,
                                 CLOCK_GALLERY_IMAGE_WIDTH,
                                 CLOCK_GALLERY_IMAGE_HEIGHT,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_canvas_fill_bg(g_gallery_image_canvas, lv_color_white(), LV_OPA_COVER);
        }
    }

    lv_obj_t *divider = make_bar(screen, 252, 66, 3, 188);
    set_obj_black(divider, true);
    if (!g_gallery_time_canvas_buf) {
        g_gallery_time_canvas_buf = alloc_canvas_buffer(kGalleryTimeCanvasW, kGalleryTimeCanvasH);
    }
    g_gallery_time_canvas = lv_canvas_create(screen);
    if (!g_gallery_time_canvas) {
        ESP_LOGW(TAG, "%s", GALLERY_TIME_CANVAS_CREATE_FAILED_LOG);
    } else {
        lv_obj_clear_flag(g_gallery_time_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(g_gallery_time_canvas, 268, 62);
        lv_obj_set_size(g_gallery_time_canvas, kGalleryTimeCanvasW, kGalleryTimeCanvasH);
        lv_obj_set_style_border_width(g_gallery_time_canvas, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_gallery_time_canvas, 0, LV_PART_MAIN);
        if (g_gallery_time_canvas_buf) {
            lv_canvas_set_buffer(g_gallery_time_canvas,
                                 g_gallery_time_canvas_buf,
                                 kGalleryTimeCanvasW,
                                 kGalleryTimeCanvasH,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_canvas_fill_bg(g_gallery_time_canvas, lv_color_white(), LV_OPA_COVER);
        }
    }

    g_gallery_saying_label = make_label(screen, 18, 272, 364, 26, "");
    if (!g_gallery_saying_label) {
        ESP_LOGW(TAG, "%s", GALLERY_SAYING_LABEL_CREATE_FAILED_LOG);
    } else {
        lv_obj_set_style_text_font(g_gallery_saying_label, &zh_font_16, LV_PART_MAIN);
        lv_obj_set_style_text_align(g_gallery_saying_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(g_gallery_saying_label, LV_LABEL_LONG_DOT);
    }

    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_battery_segments(g_gallery_battery_segments, g_battery_percent);
    s_last_gallery_image_index = -1;
    s_last_gallery_time_key = -1;
}
