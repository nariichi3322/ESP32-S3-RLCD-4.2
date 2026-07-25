// 构建和刷新图片时钟页面的图库、大号时间和每日文字。
#include "ui_work_pages.h"

#include "app_constexpr.h"
#include "app_metadata.h"
#include "app_time_constants.h"
#include "battery_runtime_state.h"
#include "clock_gallery_images.h"
#include "custom_assets.h"
#include "daily_saying_contract.h"
#include "daily_saying_state.h"
#include "work_page_ids.h"
#include "ui_battery.h"
#include "ui_bitmap.h"
#include "ui_canvas_primitives.h"
#include "ui_gallery_rotation_state.h"
#include "ui_gallery_selection.h"
#include "ui_fonts.h"
#include "ui_page_state.h"
#include "ui_progress.h"
#include "ui_widgets.h"
#include "ui_work_status.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stddef.h>
#include <stdint.h>

#define GALLERY_IMAGE_CANVAS_CREATE_FAILED_LOG "gallery image canvas create failed"
#define GALLERY_TIME_CANVAS_CREATE_FAILED_LOG "gallery time canvas create failed"
#define GALLERY_SAYING_LABEL_CREATE_FAILED_LOG "gallery saying label create failed"
#define GALLERY_CUSTOM_IMAGE_BUFFER_ALLOC_FAILED_LOG "custom gallery image buffer allocation failed"

struct GalleryRuntimeState {
    GalleryCustomImageRetryState custom_image_retry;
    GalleryImageRenderCache image_render_cache;
    int last_time_key;
    int last_hour;
    int last_minute;
    uint32_t last_saying_version;
    uint8_t *custom_image;
    lv_obj_t *image_canvas;
    lv_obj_t *time_canvas;
    lv_obj_t *saying_label;
    lv_color_t *image_canvas_buffer;
    lv_color_t *time_canvas_buffer;
};

EXT_RAM_BSS_ATTR GalleryRuntimeState s_gallery_runtime;
static constexpr size_t kGalleryRuntimeScalarBytes =
    sizeof(GalleryImageRenderCache) +
    sizeof(GalleryCustomImageRetryState) +
    3 * sizeof(int) +
    sizeof(uint32_t);
static constexpr size_t kGalleryRuntimePointerOffset =
    (kGalleryRuntimeScalarBytes + alignof(void *) - 1) &
    ~(alignof(void *) - 1);
static constexpr size_t kGalleryRuntimeExpectedSize =
    kGalleryRuntimePointerOffset + 6 * sizeof(void *);
static_assert(sizeof(GalleryRuntimeState) == kGalleryRuntimeExpectedSize,
              "gallery runtime state must contain only compact scalars and pointers");

static constexpr int kGalleryTopLineX = 18;
static constexpr int kGalleryTopLineY = 54;
static constexpr int kGalleryTopLineW = 364;
static constexpr int kGalleryTopLineH = 4;
static constexpr int kGalleryImageCanvasX = 20;
static constexpr int kGalleryImageCanvasY = 66;
static constexpr int kGalleryDividerX = 252;
static constexpr int kGalleryDividerY = 70;
static constexpr int kGalleryDividerW = 3;
static constexpr int kGalleryDividerH = kGalleryImageCanvasY + CLOCK_GALLERY_IMAGE_HEIGHT -
                                        kGalleryDividerY;
static constexpr int kGalleryTimeCanvasX = 268;
static constexpr int kGalleryTimeCanvasY = 66;
static constexpr int kGalleryTimeCanvasW = 112;
static constexpr int kGalleryTimeCanvasH = 198;
static constexpr int kGallerySayingLabelX = 18;
static constexpr int kGallerySayingLabelY = 275;
static constexpr int kGallerySayingLabelW = 364;
static constexpr int kGallerySayingLabelH = 24;
static constexpr int kGalleryMinutesPerHour = 60;
static constexpr int kGalleryHoursPerDay = 24;
static constexpr int kGalleryDaysPerLeapYear = 366;
static constexpr int kGalleryBlockDigitRows = 7;
static constexpr int kGalleryBlockDigitCols = 5;
static constexpr int kGalleryBlockDigitScale = 10;
static constexpr int kGalleryBlockDigitGap = 8;
static constexpr int kGalleryBlockDigitW = kGalleryBlockDigitCols * kGalleryBlockDigitScale;
static constexpr int kGalleryBlockDigitH = kGalleryBlockDigitRows * kGalleryBlockDigitScale;
static constexpr int kGalleryBlockNumberW = kGalleryBlockDigitW * 2 + kGalleryBlockDigitGap;
static constexpr int kGalleryDecimalBase = 10;
static constexpr uint8_t kGalleryTensDigitMask = 1U << 0;
static constexpr uint8_t kGalleryOnesDigitMask = 1U << 1;
static constexpr uint8_t kGalleryBothDigitsMask =
    kGalleryTensDigitMask | kGalleryOnesDigitMask;
static constexpr int kGalleryTimeHourY = 15;
static constexpr int kGalleryTimeMinuteY = 116;
static constexpr size_t kCustomGalleryImageBufferSize =
    CLOCK_GALLERY_IMAGE_BYTES_PER_ROW * CLOCK_GALLERY_IMAGE_HEIGHT;

static const char *const kBlockDigits[][kGalleryBlockDigitRows] = {
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
static constexpr int kGalleryBlockDigitCount = static_cast<int>(array_count(kBlockDigits));

static constexpr int gallery_block_digit_x(int digit_index)
{
    return (kGalleryTimeCanvasW - kGalleryBlockNumberW) / 2 +
           digit_index * (kGalleryBlockDigitW + kGalleryBlockDigitGap);
}

static constexpr uint8_t gallery_changed_digit_mask(int previous, int current)
{
    if (current < 0 || current >= kGalleryDecimalBase * kGalleryDecimalBase) {
        return 0;
    }
    if (previous < 0 || previous >= kGalleryDecimalBase * kGalleryDecimalBase) {
        return kGalleryBothDigitsMask;
    }
    return static_cast<uint8_t>(
        ((previous / kGalleryDecimalBase) != (current / kGalleryDecimalBase)
             ? kGalleryTensDigitMask
             : 0U) |
        ((previous % kGalleryDecimalBase) != (current % kGalleryDecimalBase)
             ? kGalleryOnesDigitMask
             : 0U));
}

static_assert(kGalleryBlockDigitCount > 0, "Gallery block digit table must not be empty");
static_assert(kGalleryTopLineW > 0 && kGalleryTopLineH > 0, "Gallery top separator size must be positive");
static_assert(kGalleryDividerW > 0 && kGalleryDividerH > 0, "Gallery divider size must be positive");
static_assert(kGalleryDividerY + kGalleryDividerH == kGalleryImageCanvasY + CLOCK_GALLERY_IMAGE_HEIGHT,
              "Gallery divider bottom must align with the image canvas bottom");
static_assert(kGalleryTimeCanvasW > 0 && kGalleryTimeCanvasH > 0, "Gallery time canvas dimensions must be positive");
static_assert(kGallerySayingLabelW > 0 && kGallerySayingLabelH > 0, "Gallery saying label size must be positive");
static_assert(kGalleryMinutesPerHour > 0, "Gallery minutes per hour must be positive");
static_assert(kGalleryHoursPerDay > 0, "Gallery hours per day must be positive");
static_assert(kGalleryDaysPerLeapYear > 0, "Gallery year span must be positive");
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
static_assert(kGalleryTimeHourY + kGalleryBlockDigitH < kGalleryTimeMinuteY,
              "Gallery hour and minute dirty regions must not overlap");
static_assert(gallery_block_digit_x(0) >= 0, "Gallery first digit must fit the time canvas");
static_assert(gallery_block_digit_x(0) + kGalleryBlockDigitW <= gallery_block_digit_x(1),
              "Gallery block digit dirty regions must not overlap");
static_assert(gallery_block_digit_x(1) + kGalleryBlockDigitW <= kGalleryTimeCanvasW,
              "Gallery second digit must fit the time canvas");
static_assert(gallery_changed_digit_mask(-1, 0) == kGalleryBothDigitsMask,
              "Gallery initial draw must update both digits");
static_assert(gallery_changed_digit_mask(34, 35) == kGalleryOnesDigitMask,
              "Gallery ordinary minute change must update only the ones digit");
static_assert(gallery_changed_digit_mask(39, 40) == kGalleryBothDigitsMask,
              "Gallery decimal rollover must update both digits");
static_assert(gallery_changed_digit_mask(10, 10) == 0,
              "Gallery unchanged value must not redraw digits");
static_assert(kCustomGalleryImageBufferSize > 0,
              "custom gallery image buffer size must be positive");

static void invalidate_gallery_draw_cache()
{
    gallery_image_render_cache_reset(&s_gallery_runtime.image_render_cache);
    gallery_custom_image_retry_reset(&s_gallery_runtime.custom_image_retry);
    s_gallery_runtime.last_time_key = -1;
    s_gallery_runtime.last_hour = -1;
    s_gallery_runtime.last_minute = -1;
    s_gallery_runtime.last_saying_version = UINT32_MAX;
}

static uint32_t gallery_minute_key(const struct tm &local)
{
    const int64_t key =
        (((static_cast<int64_t>(local.tm_year) * kGalleryDaysPerLeapYear) +
          local.tm_yday) *
             kGalleryHoursPerDay +
         local.tm_hour) *
            kGalleryMinutesPerHour +
        local.tm_min;
    return static_cast<uint32_t>(key);
}

static uint8_t *ensure_custom_gallery_image_buffer()
{
    if (s_gallery_runtime.custom_image) {
        return s_gallery_runtime.custom_image;
    }
    s_gallery_runtime.custom_image = static_cast<uint8_t *>(heap_caps_malloc(
        kCustomGalleryImageBufferSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_gallery_runtime.custom_image) {
        s_gallery_runtime.custom_image = static_cast<uint8_t *>(heap_caps_malloc(
            kCustomGalleryImageBufferSize,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!s_gallery_runtime.custom_image) {
        ESP_LOGW(TAG, "%s", GALLERY_CUSTOM_IMAGE_BUFFER_ALLOC_FAILED_LOG);
    }
    return s_gallery_runtime.custom_image;
}

static void image_fill_rect(lv_img_dsc_t *image,
                            int x,
                            int y,
                            int w,
                            int h,
                            lv_color_t color)
{
    if (!image || image->header.w == 0 || image->header.h == 0 ||
        w <= 0 || h <= 0) {
        return;
    }
    const int image_w = image->header.w;
    const int image_h = image->header.h;
    if (x >= image_w || y >= image_h || x + w <= 0 || y + h <= 0) {
        return;
    }
    const int x1 = clamp_int(x, 0, image_w - 1);
    const int y1 = clamp_int(y, 0, image_h - 1);
    const int x2 = clamp_int(x + w - 1, 0, image_w - 1);
    const int y2 = clamp_int(y + h - 1, 0, image_h - 1);
    for (int yy = y1; yy <= y2; ++yy) {
        for (int xx = x1; xx <= x2; ++xx) {
            lv_img_buf_set_px_color(image, xx, yy, color);
        }
    }
}

static void draw_block_digit(lv_img_dsc_t *image, int digit, int x, int y, int scale)
{
    if (digit < 0 || digit >= kGalleryBlockDigitCount) {
        return;
    }
    for (int row = 0; row < kGalleryBlockDigitRows; ++row) {
        for (int col = 0; col < kGalleryBlockDigitCols; ++col) {
            if (kBlockDigits[digit][row][col] == '1') {
                image_fill_rect(image,
                                x + col * scale,
                                y + row * scale,
                                scale - 1,
                                scale - 1,
                                lv_color_black());
            }
        }
    }
}

static bool update_block_number(lv_img_dsc_t *image,
                                lv_obj_t *canvas,
                                int value,
                                int y,
                                int *last_value)
{
    if (!image || !canvas || !last_value) {
        return false;
    }
    const uint8_t changed_mask = gallery_changed_digit_mask(*last_value, value);
    if (changed_mask == 0) {
        return false;
    }
    const int digits[] = {
        value / kGalleryDecimalBase,
        value % kGalleryDecimalBase,
    };
    const uint8_t masks[] = {
        kGalleryTensDigitMask,
        kGalleryOnesDigitMask,
    };
    for (int digit_index = 0; digit_index < 2; ++digit_index) {
        if ((changed_mask & masks[digit_index]) == 0) {
            continue;
        }
        const int x = gallery_block_digit_x(digit_index);
        image_fill_rect(image,
                        x,
                        y,
                        kGalleryBlockDigitW,
                        kGalleryBlockDigitH,
                        lv_color_white());
        draw_block_digit(image, digits[digit_index], x, y, kGalleryBlockDigitScale);
        invalidate_canvas_rect(canvas,
                               x,
                               y,
                               x + kGalleryBlockDigitW - 1,
                               y + kGalleryBlockDigitH - 1);
    }
    *last_value = value;
    return true;
}

static bool update_gallery_time_labels(const struct tm &local)
{
    if (!s_gallery_runtime.time_canvas ||
        !s_gallery_runtime.time_canvas_buffer) {
        return false;
    }
    lv_img_dsc_t *image = lv_canvas_get_img(s_gallery_runtime.time_canvas);
    if (!image) {
        return false;
    }
    bool changed = update_block_number(image,
                                       s_gallery_runtime.time_canvas,
                                       local.tm_hour,
                                       kGalleryTimeHourY,
                                       &s_gallery_runtime.last_hour);
    changed |= update_block_number(image,
                                   s_gallery_runtime.time_canvas,
                                   local.tm_min,
                                   kGalleryTimeMinuteY,
                                   &s_gallery_runtime.last_minute);
    return changed;
}

static bool update_gallery_saying_label()
{
    if (!s_gallery_runtime.saying_label) {
        return false;
    }
    const uint32_t version = daily_saying_state_version_load();
    if (version == s_gallery_runtime.last_saying_version) {
        return false;
    }
    char saying[kDailySayingLen] = {};
    get_daily_saying_snapshot(saying, sizeof(saying));
    s_gallery_runtime.last_saying_version = version;
    return set_label_text_if_changed(s_gallery_runtime.saying_label, saying);
}

static void style_gallery_saying_label(lv_obj_t *label)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_text_font(label, &zh_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

static void build_gallery_canvas(lv_obj_t *screen,
                                 lv_obj_t **canvas,
                                 lv_color_t **buffer,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 const char *failure_log)
{
    if (!screen || !canvas || !buffer || width <= 0 || height <= 0) {
        return;
    }
    if (!ensure_canvas_buffer(buffer, width, height)) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, "%s", failure_log);
        return;
    }
    configure_canvas_base(*canvas, *buffer, x, y, width, height);
    lv_canvas_fill_bg(*canvas, lv_color_white(), LV_OPA_COVER);
}

static bool update_gallery_image_for_date(const struct tm &local)
{
    if (!s_gallery_runtime.image_canvas ||
        !s_gallery_runtime.image_canvas_buffer) {
        return false;
    }
    int custom_count = custom_assets_gallery_count();
    GalleryImageSelection selection = {};
    const int rotation_minutes = effective_gallery_rotation_minutes(
        gallery_rotation_period_load(), custom_count);
    if (!gallery_image_selection_for_time(local.tm_year + kTmYearOffset,
                                          local.tm_mon + kTmMonthOffset,
                                          local.tm_mday,
                                          local.tm_hour,
                                          local.tm_min,
                                          local.tm_wday,
                                          custom_count,
                                          CLOCK_GALLERY_IMAGE_COUNT,
                                          rotation_minutes,
                                          &selection)) {
        return false;
    }
    const uint32_t minute_key = gallery_minute_key(local);
    const bool custom_retry_pending =
        selection.uses_custom_gallery &&
        gallery_custom_image_retry_pending(
            s_gallery_runtime.custom_image_retry,
            selection.image_index);
    const bool selected_image_drawn =
        gallery_image_render_cache_matches(
            s_gallery_runtime.image_render_cache,
            selection,
            selection.uses_custom_gallery);
    const bool fallback_image_drawn =
        selection.uses_custom_gallery &&
        gallery_image_render_cache_matches(
            s_gallery_runtime.image_render_cache,
            selection,
            false);
    if (selected_image_drawn ||
        (!custom_retry_pending && fallback_image_drawn)) {
        return false;
    }

    const uint8_t *image_bits = clock_gallery_images[selection.builtin_index];
    bool used_custom_image = false;
    if (selection.uses_custom_gallery) {
        const bool should_attempt = gallery_custom_image_should_attempt(
            s_gallery_runtime.custom_image_retry,
            selection.image_index,
            minute_key);
        if (should_attempt) {
            uint8_t *custom_image = ensure_custom_gallery_image_buffer();
            used_custom_image =
                custom_image &&
                custom_assets_read_gallery_image(selection.image_index,
                                                 custom_image,
                                                 kCustomGalleryImageBufferSize);
            gallery_custom_image_record_result(
                &s_gallery_runtime.custom_image_retry,
                selection.image_index,
                minute_key,
                used_custom_image);
            if (used_custom_image) {
                image_bits = custom_image;
            }
        }
        if (!used_custom_image && fallback_image_drawn) {
            return false;
        }
    } else {
        gallery_custom_image_retry_reset(
            &s_gallery_runtime.custom_image_retry);
    }

    draw_1bit_icon(s_gallery_runtime.image_canvas,
                   CLOCK_GALLERY_IMAGE_WIDTH,
                   CLOCK_GALLERY_IMAGE_HEIGHT,
                   CLOCK_GALLERY_IMAGE_BYTES_PER_ROW,
                   image_bits,
                   lv_color_black(),
                   lv_color_white());
    gallery_image_render_cache_record(
        &s_gallery_runtime.image_render_cache,
        selection,
        used_custom_image);
    return true;
}

bool update_gallery_page(const struct tm &local)
{
    build_gallery_page();
    bool changed = false;
    int time_key = local.tm_hour * kGalleryMinutesPerHour + local.tm_min;
    if (time_key != s_gallery_runtime.last_time_key) {
        if (update_gallery_time_labels(local)) {
            s_gallery_runtime.last_time_key = time_key;
            changed = true;
        }
    }
    changed |= update_gallery_image_for_date(local);
    changed |= update_gallery_saying_label();
    return changed;
}

void build_gallery_page()
{
    if (work_page_root(kWorkPageGallery)) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    set_work_page_root(kWorkPageGallery, screen);

    build_work_page_battery_icon(screen, kWorkPageGallery);
    build_work_page_status_bar(screen,
                               kWorkPageGallery,
                               true,
                               false);

    lv_obj_t *top_line = make_bar(screen,
                                  kGalleryTopLineX,
                                  kGalleryTopLineY,
                                  kGalleryTopLineW,
                                  kGalleryTopLineH);
    set_obj_black(top_line, true);
    build_work_page_day_progress(screen, kWorkPageGallery);

    build_gallery_canvas(screen,
                         &s_gallery_runtime.image_canvas,
                         &s_gallery_runtime.image_canvas_buffer,
                         kGalleryImageCanvasX,
                         kGalleryImageCanvasY,
                         CLOCK_GALLERY_IMAGE_WIDTH,
                         CLOCK_GALLERY_IMAGE_HEIGHT,
                         GALLERY_IMAGE_CANVAS_CREATE_FAILED_LOG);

    lv_obj_t *divider = make_bar(screen,
                                 kGalleryDividerX,
                                 kGalleryDividerY,
                                 kGalleryDividerW,
                                 kGalleryDividerH);
    set_obj_black(divider, true);
    build_gallery_canvas(screen,
                         &s_gallery_runtime.time_canvas,
                         &s_gallery_runtime.time_canvas_buffer,
                         kGalleryTimeCanvasX,
                         kGalleryTimeCanvasY,
                         kGalleryTimeCanvasW,
                         kGalleryTimeCanvasH,
                         GALLERY_TIME_CANVAS_CREATE_FAILED_LOG);

    s_gallery_runtime.saying_label = make_label(screen,
                                                kGallerySayingLabelX,
                                                kGallerySayingLabelY,
                                                kGallerySayingLabelW,
                                                kGallerySayingLabelH,
                                                "");
    if (!s_gallery_runtime.saying_label) {
        ESP_LOGW(TAG, "%s", GALLERY_SAYING_LABEL_CREATE_FAILED_LOG);
    } else {
        style_gallery_saying_label(s_gallery_runtime.saying_label);
    }

    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_work_page_battery_icon(kWorkPageGallery, battery_percent_load());
    invalidate_gallery_draw_cache();
}

void clear_gallery_object_refs()
{
    s_gallery_runtime.image_canvas = nullptr;
    s_gallery_runtime.time_canvas = nullptr;
    s_gallery_runtime.saying_label = nullptr;
    invalidate_gallery_draw_cache();
}
