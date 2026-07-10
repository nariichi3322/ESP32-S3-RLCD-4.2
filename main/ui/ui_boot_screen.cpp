// 构建启动屏、播放启动动画并在启动完成后切换到首个已启用工作页。
#include "ui_views.h"

namespace {
constexpr uint32_t kBootAnimLvglLockTimeoutMs = 100;
constexpr uint32_t kBootAnimFinishLvglLockTimeoutMs = 200;
constexpr uint32_t kBootAnimFinishHoldMs = 100;
constexpr uint32_t kBootScreenLvglLockTimeoutMs = 2000;
constexpr int kBootContentX = 28;
constexpr int kBootContentW = 344;
constexpr int kBootTitleY = 30;
constexpr int kBootTitleH = 30;
constexpr int kBootStatusY = 64;
constexpr int kBootStatusH = 24;
constexpr int kBootVersionY = 226;
constexpr int kBootVersionH = 24;
constexpr int kBootDetailY = 256;
constexpr int kBootDetailH = 22;
constexpr int kBootAnimCanvasX = 144;
constexpr int kBootAnimCanvasY = 100;
constexpr const char *kBootTitleText = "RLCD Weather Clock";
constexpr const char *kBootInitialStatusText = "Starting...";
constexpr const char *kBootInitialDetailText = "Preparing system";
#define BOOT_ANIM_DONE_EVENT_SKIPPED_LOG "boot anim done event skipped: app events unavailable"
#define BOOT_ANIM_CANVAS_CREATE_FAILED_LOG "boot anim canvas create failed"
constexpr const char *const kBootTexts[] = {
    kBootTitleText,
    kBootInitialStatusText,
    kBootInitialDetailText,
    BOOT_ANIM_DONE_EVENT_SKIPPED_LOG,
    BOOT_ANIM_CANVAS_CREATE_FAILED_LOG,
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

static_assert(kBootContentX >= 0 && kBootContentW > 0,
              "boot screen content frame must be valid");
static_assert(kBootTitleH > 0 && kBootStatusH > 0 && kBootVersionH > 0 && kBootDetailH > 0,
              "boot screen label heights must be positive");
static_assert(kBootAnimCanvasX >= 0 && kBootAnimCanvasY >= 0,
              "boot animation canvas position must be non-negative");
static_assert(kBootAnimLvglLockTimeoutMs > 0, "boot animation LVGL lock timeout must be positive");
static_assert(kBootAnimFinishLvglLockTimeoutMs >= kBootAnimLvglLockTimeoutMs,
              "boot animation finish lock timeout must cover the normal lock timeout");
static_assert(kBootScreenLvglLockTimeoutMs >= kBootAnimFinishLvglLockTimeoutMs,
              "boot screen lock timeout must cover boot animation finish lock timeout");
static_assert(cstr_array_nonempty(kBootTexts), "boot screen text registry must not be empty");
} // namespace

void draw_boot_anim_frame_index(int frame)
{
    if (!g_boot_anim_canvas) {
        return;
    }
    if (frame < 0) {
        frame = 0;
    } else if (frame >= BOOT_ANIM_FRAME_COUNT) {
        frame = BOOT_ANIM_FRAME_COUNT - 1;
    }
    const uint8_t *pixels = boot_anim_frames[frame];
    uint32_t bit = 0;
    for (int y = 0; y < BOOT_ANIM_HEIGHT; ++y) {
        for (int x = 0; x < BOOT_ANIM_WIDTH; ++x, ++bit) {
            bool black = packed_1bit_bit_is_set(pixels, bit);
            lv_canvas_set_px_color(g_boot_anim_canvas, x, y, black ? lv_color_black() : lv_color_white());
        }
    }
    lv_obj_invalidate(g_boot_anim_canvas);
}

void boot_anim_task(void *)
{
    int frame = 0;
    while (g_boot_anim_running) {
        if (Lvgl_lock(kBootAnimLvglLockTimeoutMs)) {
            draw_boot_anim_frame_index(frame);
            g_boot_anim_current_frame = frame;
            Lvgl_unlock();
        }
        frame = (frame + 1) % BOOT_ANIM_FRAME_COUNT;
        vTaskDelay(pdMS_TO_TICKS(kBootAnimRunFrameMs));
    }
    if (g_app_events) {
        xEventGroupSetBits(g_app_events, kBootAnimDoneBit);
    } else {
        ESP_LOGW(TAG, BOOT_ANIM_DONE_EVENT_SKIPPED_LOG);
    }
    g_boot_anim_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void finish_boot_anim_to_last_frame()
{
    if (Lvgl_lock(kBootAnimFinishLvglLockTimeoutMs)) {
        draw_boot_anim_frame_index(BOOT_ANIM_FRAME_COUNT - 1);
        g_boot_anim_current_frame = BOOT_ANIM_FRAME_COUNT - 1;
        lv_refr_now(nullptr);
        Lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(kBootAnimFinishHoldMs));
}

void show_boot_screen()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    make_centered_label_with_font(screen, kBootContentX, kBootTitleY, kBootContentW, kBootTitleH,
                                  kBootTitleText, &lv_font_montserrat_16, "boot title create failed");
    g_boot_status_label = make_centered_label_with_font(screen, kBootContentX, kBootStatusY, kBootContentW,
                                                        kBootStatusH, kBootInitialStatusText,
                                                        &lv_font_montserrat_16, "boot status label create failed");
    g_boot_detail_label = make_centered_label_with_font(screen, kBootContentX, kBootDetailY, kBootContentW,
                                                        kBootDetailH, kBootInitialDetailText,
                                                        &lv_font_montserrat_14, "boot detail label create failed");
    make_centered_label_with_font(screen, kBootContentX, kBootVersionY, kBootContentW, kBootVersionH,
                                  APP_VERSION, &lv_font_montserrat_16, "boot version label create failed");

    if (!g_boot_anim_canvas_buf) {
        g_boot_anim_canvas_buf = alloc_canvas_buffer(BOOT_ANIM_WIDTH, BOOT_ANIM_HEIGHT);
    }
    g_boot_anim_canvas = lv_canvas_create(screen);
    if (g_boot_anim_canvas) {
        lv_obj_clear_flag(g_boot_anim_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(g_boot_anim_canvas, kBootAnimCanvasX, kBootAnimCanvasY);
        lv_obj_set_size(g_boot_anim_canvas, BOOT_ANIM_WIDTH, BOOT_ANIM_HEIGHT);
        lv_obj_set_style_border_width(g_boot_anim_canvas, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_boot_anim_canvas, 0, LV_PART_MAIN);
    } else {
        ESP_LOGW(TAG, BOOT_ANIM_CANVAS_CREATE_FAILED_LOG);
    }
    if (g_boot_anim_canvas && g_boot_anim_canvas_buf) {
        lv_canvas_set_buffer(g_boot_anim_canvas, g_boot_anim_canvas_buf,
                             BOOT_ANIM_WIDTH, BOOT_ANIM_HEIGHT, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(g_boot_anim_canvas, lv_color_white(), LV_OPA_COVER);
        draw_boot_anim_frame_index(0);
    }
    lv_refr_now(nullptr);
}

void update_boot_screen(int percent, const char *status, const char *detail)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    if (Lvgl_lock(kBootScreenLvglLockTimeoutMs)) {
        if (g_boot_status_label) {
            set_label_text_if_changed(g_boot_status_label, status);
        }
        if (g_boot_detail_label) {
            set_label_text_if_changed(g_boot_detail_label, detail);
        }
        lv_refr_now(nullptr);
        Lvgl_unlock();
    }
}

void finish_boot_screen()
{
    if (Lvgl_lock(kBootScreenLvglLockTimeoutMs)) {
        lv_obj_clean(lv_scr_act());
        clear_clock_object_refs();
        clear_info_object_refs();
        g_boot_status_label = nullptr;
        g_boot_detail_label = nullptr;
        g_boot_anim_canvas = nullptr;
        g_active_work_page = first_enabled_work_page();
        show_active_work_page();
        lv_refr_now(nullptr);
        Lvgl_unlock();
    }
}
