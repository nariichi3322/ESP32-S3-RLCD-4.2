// 绘制第六页温湿时钟，秒级只局部刷新对应数字牌。
#include "ui_views.h"

#include "calendar_lunar.h"
#include "flip_sensor_icons.h"

#include <algorithm>

#define FLIP_CLOCK_CARD_CANVAS_CREATE_FAILED_FORMAT "flip clock card %d canvas create failed"
#define FLIP_CLOCK_TEMP_LABEL_CREATE_FAILED_LOG "flip clock temp label create failed"
#define FLIP_CLOCK_HUMIDITY_LABEL_CREATE_FAILED_LOG "flip clock humidity label create failed"
#define FLIP_CLOCK_MOOD_CANVAS_CREATE_FAILED_FORMAT "flip clock %s mood canvas create failed"
#define FLIP_CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT "flip clock %s trend canvas create failed"

namespace {

static constexpr int kCardCount = 3;
static constexpr int kCardW = 112;
static constexpr int kCardH = 112;
static constexpr int kCardY = 66;
static constexpr int kCardRadius = 8;
static constexpr int kCardX[kCardCount] = {18, 144, 270};
static constexpr int kHourCardIndex = 0;
static constexpr int kMinuteCardIndex = 1;
static constexpr int kSecondCardIndex = 2;
static constexpr int kSecondsPerMinute = 60;
static constexpr int kMinutesPerHour = 60;
static constexpr int kHoursPerDay = 24;
static constexpr int kProgressSegmentCount = 60;
static constexpr int kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
static constexpr int kSecondsPerDay = kHoursPerDay * kSecondsPerHour;
static constexpr int kTmYearOffset = 1900;
static constexpr int kTmMonthOffset = 1;
static constexpr int kDigitScaleNumerator = 3;
static constexpr int kDigitScaleDenominator = 4;
static constexpr int kDigitBaselineY = 84;
static constexpr int kFlipSensorPanelX = kCardX[kHourCardIndex];
static constexpr int kFlipSensorPanelY = 198;
static constexpr int kFlipSensorPanelW = kCardX[kMinuteCardIndex] + kCardW - kFlipSensorPanelX;
static constexpr int kFlipSensorPanelH = 88;
static constexpr int kFlipSensorPanelRadius = 18;
static constexpr int kFlipSensorTempTextY = 204;
static constexpr int kFlipSensorHumiTextY = 243;
static constexpr int kFlipSensorTextH = 36;
static constexpr int kFlipSensorTextPadX = 16;
static constexpr int kFlipSensorTextW = 148;
static constexpr int kFlipSensorBoldOffset = 1;
static constexpr int kFlipSensorBoldYOffset = 1;
static constexpr int kFlipMoodCanvasX = kFlipSensorPanelX + kFlipSensorPanelW - FLIP_SENSOR_ICON_WIDTH - 16;
static constexpr int kFlipTempMoodCanvasY = 204;
static constexpr int kFlipHumiMoodCanvasY = 244;
static constexpr int kFlipTrendCanvasW = 20;
static constexpr int kFlipTrendCanvasH = 20;
static constexpr int kFlipTrendCanvasX = kFlipMoodCanvasX - kFlipTrendCanvasW - 8;
static constexpr int kFlipTempTrendCanvasY = kFlipTempMoodCanvasY + (FLIP_SENSOR_ICON_HEIGHT - kFlipTrendCanvasH) / 2;
static constexpr int kFlipHumiTrendCanvasY = kFlipHumiMoodCanvasY + (FLIP_SENSOR_ICON_HEIGHT - kFlipTrendCanvasH) / 2;
static constexpr int kFlipTrendIconX = (kFlipTrendCanvasW - TREND_ICON_WIDTH) / 2;
static constexpr int kFlipTrendIconY = (kFlipTrendCanvasH - TREND_ICON_HEIGHT) / 2;
static constexpr int kFlipDatePanelX = 270;
static constexpr int kFlipDatePanelY = kFlipSensorPanelY;
static constexpr int kFlipDatePanelW = kCardW;
static constexpr int kFlipDatePanelH = kFlipSensorPanelH;
static constexpr int kFlipDatePanelRadius = kFlipSensorPanelRadius;
static constexpr int kFlipDayTextY = 196;
static constexpr int kFlipDayTextH = 52;
static constexpr int kFlipLunarTextY = 249;
static constexpr int kFlipLunarTextH = 42;
static constexpr int kFlipDateBoldOffset = 1;
static constexpr int kFlipDateBoldYOffset = 1;
static constexpr size_t kFlipSensorTextSize = 16;
static constexpr size_t kFlipDayTextSize = 8;
static constexpr int kSensorMoodComfort = 0;
static constexpr int kSensorMoodOk = 1;
static constexpr int kSensorMoodBad = 2;
static constexpr int kSensorMoodUnavailable = -1;
static constexpr float kComfortTempMinC = 20.0f;
static constexpr float kComfortTempMaxC = 26.0f;
static constexpr float kOkTempMinC = 18.0f;
static constexpr float kOkTempMaxC = 30.0f;
static constexpr float kComfortHumiMinPercent = 40.0f;
static constexpr float kComfortHumiMaxPercent = 60.0f;
static constexpr float kOkHumiMinPercent = 30.0f;
static constexpr float kOkHumiMaxPercent = 70.0f;
static constexpr const char *kFlipTempPlaceholder = "--.-C";
static constexpr const char *kFlipHumiPlaceholder = "--%";
static constexpr const char *kFlipDayPlaceholder = "--";
static constexpr const char *kFlipTempFormat = "%.1fC";
static constexpr const char *kFlipHumiFormat = "%.0f%%";
static constexpr const char *kFlipDayFormat = "%d";

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

static_assert(array_count(kCardX) == kCardCount,
              "flip clock card X table must match card count");
static_assert(kOkTempMinC <= kComfortTempMinC && kComfortTempMaxC <= kOkTempMaxC,
              "comfortable temperature range must stay inside ok temperature range");
static_assert(kOkHumiMinPercent <= kComfortHumiMinPercent && kComfortHumiMaxPercent <= kOkHumiMaxPercent,
              "comfortable humidity range must stay inside ok humidity range");
static_assert(kFlipSensorPanelX == kCardX[kHourCardIndex] &&
                  kFlipSensorPanelX + kFlipSensorPanelW == kCardX[kMinuteCardIndex] + kCardW &&
                  kFlipDatePanelW == kCardW,
              "flip clock sensor panel must span hour and minute cards");
static_assert(kFlipTrendIconX >= 0 && kFlipTrendIconY >= 0, "flip clock trend icon must fit canvas");

void apply_card_rounding(lv_obj_t *canvas)
{
    if (!canvas) {
        return;
    }
    int radius = kCardRadius;
    int r2 = radius * radius;
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            int dx = radius - 1 - x;
            int dy = radius - 1 - y;
            if (dx * dx + dy * dy > r2) {
                canvas_set_px_safe(canvas, x, y, kCardW, kCardH, lv_color_white());
                canvas_set_px_safe(canvas, kCardW - 1 - x, y, kCardW, kCardH, lv_color_white());
                canvas_set_px_safe(canvas, x, kCardH - 1 - y, kCardW, kCardH, lv_color_white());
                canvas_set_px_safe(canvas, kCardW - 1 - x, kCardH - 1 - y, kCardW, kCardH, lv_color_white());
            }
        }
    }
}

bool dseg_pixel_on(const DsegFont &font, const DsegGlyph *glyph, int x, int y)
{
    uint32_t bit = (uint32_t)y * glyph->width + x;
    return packed_1bit_bit_is_set(font.bitmap + glyph->bitmap_offset, bit);
}

void draw_scaled_dseg_digit(lv_obj_t *canvas,
                            const DsegGlyph *glyph,
                            int origin_x,
                            int origin_y,
                            int scale_num,
                            int scale_den,
                            int clip_y0 = 0,
                            int clip_y1 = kCardH)
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
            if (py >= clip_y0 && py < clip_y1 && dseg_pixel_on(kDSEG84Font, glyph, src_x, src_y)) {
                canvas_set_px_safe(canvas, dst_x + x, dst_y + y, kCardW, kCardH, lv_color_white());
            }
        }
    }
}

void draw_card_shell(lv_obj_t *canvas)
{
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
}

void draw_card_digits(lv_obj_t *canvas, int value, int clip_y0 = 0, int clip_y1 = kCardH)
{
    const DsegGlyph *tens = find_dseg_glyph(kDSEG84Font, (char)('0' + value / 10));
    const DsegGlyph *ones = find_dseg_glyph(kDSEG84Font, (char)('0' + value % 10));
    if (!tens || !ones) {
        return;
    }
    auto scaled = [=](int value) {
        return (value * kDigitScaleNumerator) / kDigitScaleDenominator;
    };
    auto scaled_size = [=](int value) {
        return (value * kDigitScaleNumerator + kDigitScaleDenominator - 1) / kDigitScaleDenominator;
    };
    int tens_origin = 0;
    int ones_origin = scaled(tens->x_advance);
    int left = std::min(tens_origin + scaled(tens->x_offset),
                        ones_origin + scaled(ones->x_offset));
    int right = std::max(tens_origin + scaled(tens->x_offset) + scaled_size(tens->width),
                         ones_origin + scaled(ones->x_offset) + scaled_size(ones->width));
    int x = (kCardW - (right - left)) / 2 - left;
    draw_scaled_dseg_digit(canvas,
                           tens,
                           x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
    x += scaled(tens->x_advance);
    draw_scaled_dseg_digit(canvas,
                           ones,
                           x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
}

void draw_flip_card(int card_index, int value)
{
    if (card_index < 0 || card_index >= kCardCount || !g_flip_clock_card_canvas[card_index]) {
        return;
    }
    lv_obj_t *canvas = g_flip_clock_card_canvas[card_index];
    draw_card_shell(canvas);
    draw_card_digits(canvas, value);
    apply_card_rounding(canvas);
    lv_obj_invalidate(canvas);
}

int temperature_mood(float temperature)
{
    if (temperature >= kComfortTempMinC && temperature <= kComfortTempMaxC) {
        return kSensorMoodComfort;
    }
    if (temperature >= kOkTempMinC && temperature <= kOkTempMaxC) {
        return kSensorMoodOk;
    }
    return kSensorMoodBad;
}

int humidity_mood(float humidity)
{
    if (humidity >= kComfortHumiMinPercent && humidity <= kComfortHumiMaxPercent) {
        return kSensorMoodComfort;
    }
    if (humidity >= kOkHumiMinPercent && humidity <= kOkHumiMaxPercent) {
        return kSensorMoodOk;
    }
    return kSensorMoodBad;
}

const uint8_t *temp_mood_icon_bits(int mood)
{
    if (mood == kSensorMoodComfort) {
        return flip_temp_comfort_icon_bits;
    }
    if (mood == kSensorMoodOk) {
        return flip_temp_ok_icon_bits;
    }
    if (mood == kSensorMoodBad) {
        return flip_temp_bad_icon_bits;
    }
    return nullptr;
}

const uint8_t *humi_mood_icon_bits(int mood)
{
    if (mood == kSensorMoodComfort) {
        return flip_humi_comfort_icon_bits;
    }
    if (mood == kSensorMoodOk) {
        return flip_humi_ok_icon_bits;
    }
    if (mood == kSensorMoodBad) {
        return flip_humi_bad_icon_bits;
    }
    return nullptr;
}

bool update_mood_icon(lv_obj_t *canvas, int mood, int *last_mood, const uint8_t *bits)
{
    if (!canvas || !last_mood || mood == *last_mood) {
        return false;
    }
    *last_mood = mood;
    if (!bits) {
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_invalidate(canvas);
        return true;
    }
    draw_1bit_icon(canvas,
                   FLIP_SENSOR_ICON_WIDTH,
                   FLIP_SENSOR_ICON_HEIGHT,
                   FLIP_SENSOR_ICON_BYTES_PER_ROW,
                   bits,
                   lv_color_white(),
                   lv_color_black());
    return true;
}

bool update_flip_trend_icon(lv_obj_t *canvas, int trend, int *last_trend)
{
    if (!canvas) {
        return false;
    }
    if (last_trend && trend == *last_trend) {
        return false;
    }
    if (last_trend) {
        *last_trend = trend;
    }
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    const uint8_t *bits = nullptr;
    if (trend > 0) {
        bits = trend_up_icon_bits;
    } else if (trend < 0) {
        bits = trend_down_icon_bits;
    }
    if (bits) {
        for (int y = 0; y < TREND_ICON_HEIGHT; ++y) {
            const uint8_t *row = bits + y * TREND_ICON_BYTES_PER_ROW;
            for (int x = 0; x < TREND_ICON_WIDTH; ++x) {
                if (packed_1bit_bit_is_set(row, static_cast<uint32_t>(x))) {
                    lv_canvas_set_px_color(canvas, kFlipTrendIconX + x, kFlipTrendIconY + y, lv_color_white());
                }
            }
        }
    }
    lv_obj_invalidate(canvas);
    return true;
}

template <typename... Labels>
bool set_flip_text_on_labels(const char *text, Labels... labels)
{
    bool changed = false;
    ((changed |= set_label_text_if_changed(labels, text)), ...);
    return changed;
}

template <typename... Args>
void format_flip_text_or_fallback(char *out, size_t out_len, const char *fallback, const char *format, Args... args)
{
    if (!out || out_len == 0) {
        return;
    }
    int written = snprintf(out, out_len, format, args...);
    if (written < 0 || static_cast<size_t>(written) >= out_len) {
        strlcpy(out, fallback, out_len);
    }
}

bool update_flip_sensor_text()
{
    if (!g_flip_clock_sensor_label &&
        !g_flip_clock_sensor_bold_label &&
        !g_flip_clock_humidity_label &&
        !g_flip_clock_humidity_bold_label) {
        return false;
    }
    char temp_text[kFlipSensorTextSize] = {};
    char humi_text[kFlipSensorTextSize] = {};
    if (g_sensor_ok) {
        format_flip_text_or_fallback(temp_text, sizeof(temp_text), kFlipTempPlaceholder, kFlipTempFormat, g_temperature);
        format_flip_text_or_fallback(humi_text, sizeof(humi_text), kFlipHumiPlaceholder, kFlipHumiFormat, g_humidity);
    } else {
        strlcpy(temp_text, kFlipTempPlaceholder, sizeof(temp_text));
        strlcpy(humi_text, kFlipHumiPlaceholder, sizeof(humi_text));
    }
    int temp_mood = g_sensor_ok ? temperature_mood(g_temperature) : kSensorMoodUnavailable;
    int humi_mood = g_sensor_ok ? humidity_mood(g_humidity) : kSensorMoodUnavailable;
    bool changed = false;
    changed |= set_flip_text_on_labels(temp_text,
                                       g_flip_clock_sensor_label,
                                       g_flip_clock_sensor_bold_label,
                                       g_flip_clock_sensor_bold_y_label);
    changed |= set_flip_text_on_labels(humi_text,
                                       g_flip_clock_humidity_label,
                                       g_flip_clock_humidity_bold_label,
                                       g_flip_clock_humidity_bold_y_label);
    changed |= update_mood_icon(g_flip_clock_temp_mood_canvas,
                                temp_mood,
                                &g_last_flip_temp_mood,
                                temp_mood_icon_bits(temp_mood));
    changed |= update_mood_icon(g_flip_clock_humi_mood_canvas,
                                humi_mood,
                                &g_last_flip_humi_mood,
                                humi_mood_icon_bits(humi_mood));
    changed |= update_flip_trend_icon(g_flip_clock_temp_trend_canvas,
                                      g_sensor_ok ? g_temp_trend : 0,
                                      &g_last_flip_temp_trend);
    changed |= update_flip_trend_icon(g_flip_clock_humi_trend_canvas,
                                      g_sensor_ok ? g_humi_trend : 0,
                                      &g_last_flip_humi_trend);
    return changed;
}

bool update_flip_date_text(const struct tm &local)
{
    if (!g_flip_clock_day_label &&
        !g_flip_clock_day_bold_label &&
        !g_flip_clock_day_bold_y_label &&
        !g_flip_clock_lunar_label &&
        !g_flip_clock_lunar_bold_x_label &&
        !g_flip_clock_lunar_bold_y_label &&
        !g_flip_clock_lunar_bold_xy_label) {
        return false;
    }
    CalendarDayInfo info = {};
    bool lunar_ok = calendar_day_info(local, &info);
    char day_text[kFlipDayTextSize] = {};
    format_flip_text_or_fallback(day_text, sizeof(day_text), kFlipDayPlaceholder, kFlipDayFormat, local.tm_mday);
    const char *lunar_text = lunar_ok && info.subtext[0] ? info.subtext : kFlipDayPlaceholder;
    bool changed = false;
    changed |= set_flip_text_on_labels(day_text,
                                       g_flip_clock_day_label,
                                       g_flip_clock_day_bold_label,
                                       g_flip_clock_day_bold_y_label);
    changed |= set_flip_text_on_labels(lunar_text,
                                       g_flip_clock_lunar_label,
                                       g_flip_clock_lunar_bold_x_label,
                                       g_flip_clock_lunar_bold_y_label,
                                       g_flip_clock_lunar_bold_xy_label);
    return changed;
}

int flip_date_key(const struct tm &local)
{
    return (local.tm_year + kTmYearOffset) * 10000 +
           (local.tm_mon + kTmMonthOffset) * 100 +
           local.tm_mday;
}

void style_flip_white_label(lv_obj_t *label, lv_text_align_t align)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
}

lv_obj_t *make_flip_sensor_value_label(lv_obj_t *parent,
                                        int x,
                                        int y,
                                        const char *text,
                                        lv_text_align_t align)
{
    lv_obj_t *label = make_label_with_font(parent,
                                           x,
                                           y,
                                           kFlipSensorTextW,
                                           kFlipSensorTextH,
                                           text,
                                           &lv_font_montserrat_24);
    if (!label) {
        return nullptr;
    }
    style_flip_white_label(label, align);
    return label;
}

lv_obj_t *make_flip_date_label(lv_obj_t *parent,
                               int x,
                               int y,
                               int w,
                               int h,
                               const char *text,
                               const lv_font_t *font)
{
    lv_obj_t *label = make_label_with_font(parent, x, y, w, h, text, font);
    if (!label) {
        return nullptr;
    }
    style_flip_white_label(label, LV_TEXT_ALIGN_CENTER);
    return label;
}

void build_flip_mood_canvas(lv_obj_t *screen,
                            lv_obj_t **canvas,
                            lv_color_t **buffer,
                            int y,
                            const char *name)
{
    if (!canvas || !buffer) {
        return;
    }
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(FLIP_SENSOR_ICON_WIDTH, FLIP_SENSOR_ICON_HEIGHT);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, FLIP_CLOCK_MOOD_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    lv_obj_clear_flag(*canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(*canvas, kFlipMoodCanvasX, y);
    lv_obj_set_size(*canvas, FLIP_SENSOR_ICON_WIDTH, FLIP_SENSOR_ICON_HEIGHT);
    lv_obj_set_style_border_width(*canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(*canvas, 0, LV_PART_MAIN);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas,
                             *buffer,
                             FLIP_SENSOR_ICON_WIDTH,
                             FLIP_SENSOR_ICON_HEIGHT,
                             LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(*canvas, lv_color_black(), LV_OPA_COVER);
    }
}

void build_flip_trend_canvas(lv_obj_t *screen,
                             lv_obj_t **canvas,
                             lv_color_t **buffer,
                             int y,
                             int trend,
                             const char *name)
{
    if (!canvas || !buffer) {
        return;
    }
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(kFlipTrendCanvasW, kFlipTrendCanvasH);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, FLIP_CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    lv_obj_clear_flag(*canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(*canvas, kFlipTrendCanvasX, y);
    lv_obj_set_size(*canvas, kFlipTrendCanvasW, kFlipTrendCanvasH);
    lv_obj_set_style_border_width(*canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(*canvas, 0, LV_PART_MAIN);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas, *buffer, kFlipTrendCanvasW, kFlipTrendCanvasH, LV_IMG_CF_TRUE_COLOR);
        update_flip_trend_icon(*canvas, trend, nullptr);
    }
}

} // namespace

void build_flip_clock_page()
{
    if (g_flip_clock_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_flip_clock_root = screen;
    lv_obj_add_flag(g_flip_clock_root, LV_OBJ_FLAG_HIDDEN);

    build_battery_icon(screen, g_flip_clock_battery_segments);
    build_work_page_status_bar(screen,
                               kWorkPageFlipClock,
                               &g_flip_clock_date_label,
                               nullptr,
                               &g_flip_clock_status_time_label,
                               false);

    lv_obj_t *top_line = make_bar(screen, 18, 54, 364, 4);
    set_obj_black(top_line, true);
    build_progress_canvas(screen, &g_flip_clock_day_progress_canvas, &g_flip_clock_day_progress_canvas_buf, 59);

    for (int i = 0; i < kCardCount; ++i) {
        if (!g_flip_clock_card_canvas_buf[i]) {
            g_flip_clock_card_canvas_buf[i] = alloc_canvas_buffer(kCardW, kCardH);
        }
        g_flip_clock_card_canvas[i] = lv_canvas_create(screen);
        if (!g_flip_clock_card_canvas[i]) {
            ESP_LOGW(TAG, FLIP_CLOCK_CARD_CANVAS_CREATE_FAILED_FORMAT, i);
            continue;
        }
        lv_obj_clear_flag(g_flip_clock_card_canvas[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(g_flip_clock_card_canvas[i], kCardX[i], kCardY);
        lv_obj_set_size(g_flip_clock_card_canvas[i], kCardW, kCardH);
        lv_obj_set_style_border_width(g_flip_clock_card_canvas[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_flip_clock_card_canvas[i], 0, LV_PART_MAIN);
        if (g_flip_clock_card_canvas_buf[i]) {
            lv_canvas_set_buffer(g_flip_clock_card_canvas[i],
                                 g_flip_clock_card_canvas_buf[i],
                                 kCardW,
                                 kCardH,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_canvas_fill_bg(g_flip_clock_card_canvas[i], lv_color_black(), LV_OPA_COVER);
        }
    }

    lv_obj_t *sensor_panel = make_bar(screen,
                                      kFlipSensorPanelX,
                                      kFlipSensorPanelY,
                                      kFlipSensorPanelW,
                                      kFlipSensorPanelH);
    set_obj_black(sensor_panel, true);
    if (sensor_panel) {
        lv_obj_set_style_radius(sensor_panel, kFlipSensorPanelRadius, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(sensor_panel, true, LV_PART_MAIN);
    }

    int temp_x = kFlipSensorPanelX + kFlipSensorTextPadX;
    g_flip_clock_sensor_label = make_flip_sensor_value_label(screen,
                                                             temp_x,
                                                             kFlipSensorTempTextY,
                                                             kFlipTempPlaceholder,
                                                             LV_TEXT_ALIGN_LEFT);
    g_flip_clock_sensor_bold_label = make_flip_sensor_value_label(screen,
                                                                  temp_x + kFlipSensorBoldOffset,
                                                                  kFlipSensorTempTextY,
                                                                  kFlipTempPlaceholder,
                                                                  LV_TEXT_ALIGN_LEFT);
    g_flip_clock_sensor_bold_y_label = make_flip_sensor_value_label(screen,
                                                                    temp_x,
                                                                    kFlipSensorTempTextY + kFlipSensorBoldYOffset,
                                                                    kFlipTempPlaceholder,
                                                                    LV_TEXT_ALIGN_LEFT);
    if (!g_flip_clock_sensor_label || !g_flip_clock_sensor_bold_label || !g_flip_clock_sensor_bold_y_label) {
        ESP_LOGW(TAG, "%s", FLIP_CLOCK_TEMP_LABEL_CREATE_FAILED_LOG);
    }

    g_flip_clock_humidity_label = make_flip_sensor_value_label(screen,
                                                               temp_x,
                                                               kFlipSensorHumiTextY,
                                                               kFlipHumiPlaceholder,
                                                               LV_TEXT_ALIGN_LEFT);
    g_flip_clock_humidity_bold_label = make_flip_sensor_value_label(screen,
                                                                    temp_x + kFlipSensorBoldOffset,
                                                                    kFlipSensorHumiTextY,
                                                                    kFlipHumiPlaceholder,
                                                                    LV_TEXT_ALIGN_LEFT);
    g_flip_clock_humidity_bold_y_label = make_flip_sensor_value_label(screen,
                                                                      temp_x,
                                                                      kFlipSensorHumiTextY + kFlipSensorBoldYOffset,
                                                                      kFlipHumiPlaceholder,
                                                                      LV_TEXT_ALIGN_LEFT);
    if (!g_flip_clock_humidity_label || !g_flip_clock_humidity_bold_label || !g_flip_clock_humidity_bold_y_label) {
        ESP_LOGW(TAG, "%s", FLIP_CLOCK_HUMIDITY_LABEL_CREATE_FAILED_LOG);
    }
    build_flip_mood_canvas(screen,
                           &g_flip_clock_temp_mood_canvas,
                           &g_flip_clock_temp_mood_canvas_buf,
                           kFlipTempMoodCanvasY,
                           "temperature");
    build_flip_mood_canvas(screen,
                           &g_flip_clock_humi_mood_canvas,
                           &g_flip_clock_humi_mood_canvas_buf,
                           kFlipHumiMoodCanvasY,
                           "humidity");
    build_flip_trend_canvas(screen,
                            &g_flip_clock_temp_trend_canvas,
                            &g_flip_clock_temp_trend_canvas_buf,
                            kFlipTempTrendCanvasY,
                            g_sensor_ok ? g_temp_trend : 0,
                            "temperature");
    build_flip_trend_canvas(screen,
                            &g_flip_clock_humi_trend_canvas,
                            &g_flip_clock_humi_trend_canvas_buf,
                            kFlipHumiTrendCanvasY,
                            g_sensor_ok ? g_humi_trend : 0,
                            "humidity");

    lv_obj_t *date_panel = make_bar(screen,
                                    kFlipDatePanelX,
                                    kFlipDatePanelY,
                                    kFlipDatePanelW,
                                    kFlipDatePanelH);
    set_obj_black(date_panel, true);
    if (date_panel) {
        lv_obj_set_style_radius(date_panel, kFlipDatePanelRadius, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(date_panel, true, LV_PART_MAIN);
    }
    g_flip_clock_day_label = make_flip_date_label(screen,
                                                  kFlipDatePanelX,
                                                  kFlipDayTextY,
                                                  kFlipDatePanelW,
                                                  kFlipDayTextH,
                                                  "--",
                                                  &lv_font_montserrat_48);
    g_flip_clock_day_bold_label = make_flip_date_label(screen,
                                                       kFlipDatePanelX + kFlipDateBoldOffset,
                                                       kFlipDayTextY,
                                                       kFlipDatePanelW,
                                                       kFlipDayTextH,
                                                       "--",
                                                       &lv_font_montserrat_48);
    g_flip_clock_day_bold_y_label = make_flip_date_label(screen,
                                                         kFlipDatePanelX,
                                                         kFlipDayTextY + kFlipDateBoldYOffset,
                                                         kFlipDatePanelW,
                                                         kFlipDayTextH,
                                                         "--",
                                                         &lv_font_montserrat_48);
    g_flip_clock_lunar_label = make_flip_date_label(screen,
                                                    kFlipDatePanelX,
                                                    kFlipLunarTextY,
                                                    kFlipDatePanelW,
                                                    kFlipLunarTextH,
                                                    "--",
                                                    &zh_flip_lunar_22);
    g_flip_clock_lunar_bold_x_label = make_flip_date_label(screen,
                                                           kFlipDatePanelX + kFlipDateBoldOffset,
                                                           kFlipLunarTextY,
                                                           kFlipDatePanelW,
                                                           kFlipLunarTextH,
                                                           "--",
                                                           &zh_flip_lunar_22);
    g_flip_clock_lunar_bold_y_label = make_flip_date_label(screen,
                                                           kFlipDatePanelX,
                                                           kFlipLunarTextY + kFlipDateBoldYOffset,
                                                           kFlipDatePanelW,
                                                           kFlipLunarTextH,
                                                           "--",
                                                           &zh_flip_lunar_22);
    g_flip_clock_lunar_bold_xy_label = make_flip_date_label(screen,
                                                            kFlipDatePanelX + kFlipDateBoldOffset,
                                                            kFlipLunarTextY + kFlipDateBoldYOffset,
                                                            kFlipDatePanelW,
                                                            kFlipLunarTextH,
                                                            "--",
                                                            &zh_flip_lunar_22);

    update_battery_segments(g_flip_clock_battery_segments, g_battery_percent);
    g_last_flip_clock_hour = -1;
    g_last_flip_clock_minute = -1;
    g_last_flip_clock_second = -1;
    g_last_flip_day_progress_filled = -1;
    g_last_flip_second_progress_filled = -1;
    g_last_flip_sensor_minute = -1;
    g_last_flip_temp_mood = -1;
    g_last_flip_humi_mood = -1;
    g_last_flip_temp_trend = 99;
    g_last_flip_humi_trend = 99;
    g_last_flip_date_key = -1;
}

bool update_flip_clock_page(const struct tm &local)
{
    build_flip_clock_page();
    if (!g_flip_clock_root) {
        return false;
    }
    bool changed = false;
    int hour = local.tm_hour;
    int minute = local.tm_min;
    int second = local.tm_sec;
    if (hour != g_last_flip_clock_hour) {
        g_last_flip_clock_hour = hour;
        draw_flip_card(kHourCardIndex, hour);
        changed = true;
    }
    if (minute != g_last_flip_clock_minute) {
        g_last_flip_clock_minute = minute;
        draw_flip_card(kMinuteCardIndex, minute);
        changed = true;
    }
    if (second != g_last_flip_clock_second) {
        g_last_flip_clock_second = second;
        draw_flip_card(kSecondCardIndex, second);
        changed = true;
    }
    int date_key = flip_date_key(local);
    if (date_key != g_last_flip_date_key) {
        g_last_flip_date_key = date_key;
        changed |= update_flip_date_text(local);
    }

    int seconds_of_day = local.tm_hour * kSecondsPerHour + local.tm_min * kSecondsPerMinute + local.tm_sec;
    int day_filled = (seconds_of_day * kProgressSegmentCount) / kSecondsPerDay;
    update_progress_canvas(g_flip_clock_day_progress_canvas, day_filled, &g_last_flip_day_progress_filled);
    if (minute != g_last_flip_sensor_minute) {
        g_last_flip_sensor_minute = minute;
        changed |= update_flip_sensor_text();
    }
    return changed;
}
