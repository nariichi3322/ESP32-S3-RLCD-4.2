// 绘制第六页温湿时钟，秒级只局部刷新对应数字牌。
#include "ui_views.h"

#include "app_constexpr.h"
#include "calendar_lunar.h"
#include "flip_sensor_icons.h"
#include "sensor_services.h"
#include "ui_battery.h"
#include "ui_clock_time.h"
#include "ui_dseg_layout.h"
#include "ui_flip_sensor_mood.h"
#include "ui_text_format.h"

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
static constexpr int kFlipTopLineX = 18;
static constexpr int kFlipTopLineY = 54;
static constexpr int kFlipTopLineW = 364;
static constexpr int kFlipTopLineH = 4;
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
static constexpr const char *kFlipTempPlaceholder = "--.-C";
static constexpr const char *kFlipHumiPlaceholder = "--%";
static constexpr const char *kFlipDayPlaceholder = "--";
static constexpr const char *kFlipTempFormat = "%.1fC";
static constexpr const char *kFlipHumiFormat = "%.0f%%";
static constexpr const char *kFlipDayFormat = "%d";

static_assert(array_count(kCardX) == kCardCount,
              "flip clock card X table must match card count");
static_assert(kFlipTopLineW > 0 && kFlipTopLineH > 0,
              "flip clock top separator size must be positive");
static_assert(kFlipSensorPanelX == kCardX[kHourCardIndex] &&
                  kFlipSensorPanelX + kFlipSensorPanelW == kCardX[kMinuteCardIndex] + kCardW &&
                  kFlipDatePanelW == kCardW,
              "flip clock sensor panel must span hour and minute cards");
static_assert(kCardRadius > 0 && kCardRadius * 2 <= kCardW && kCardRadius * 2 <= kCardH,
              "flip clock card radius must fit card");
static_assert(kDigitScaleNumerator > 0 && kDigitScaleDenominator > 0,
              "flip clock digit scale must be positive");
static_assert(kFlipTrendIconX >= 0 && kFlipTrendIconY >= 0 &&
                  kFlipTrendIconX + TREND_ICON_WIDTH <= kFlipTrendCanvasW &&
                  kFlipTrendIconY + TREND_ICON_HEIGHT <= kFlipTrendCanvasH,
              "flip clock trend icon must fit canvas");

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
    DsegPairLayout layout = centered_dseg_pair_layout(kCardW,
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

void draw_flip_card(lv_obj_t *canvas, int value)
{
    if (!canvas) {
        return;
    }
    draw_card_shell(canvas);
    draw_card_digits(canvas, value);
    apply_card_rounding(canvas);
    lv_obj_invalidate(canvas);
}

const uint8_t *sensor_mood_icon_bits(int mood,
                                     const uint8_t *comfort_bits,
                                     const uint8_t *ok_bits,
                                     const uint8_t *bad_bits)
{
    if (mood == kSensorMoodComfort) {
        return comfort_bits;
    }
    if (mood == kSensorMoodOk) {
        return ok_bits;
    }
    if (mood == kSensorMoodBad) {
        return bad_bits;
    }
    return nullptr;
}

const uint8_t *temp_mood_icon_bits(int mood)
{
    return sensor_mood_icon_bits(mood,
                                 flip_temp_comfort_icon_bits,
                                 flip_temp_ok_icon_bits,
                                 flip_temp_bad_icon_bits);
}

const uint8_t *humi_mood_icon_bits(int mood)
{
    return sensor_mood_icon_bits(mood,
                                 flip_humi_comfort_icon_bits,
                                 flip_humi_ok_icon_bits,
                                 flip_humi_bad_icon_bits);
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
    float temperature = 0.0f;
    float humidity = 0.0f;
    int temperature_trend = 0;
    int humidity_trend = 0;
    bool sensor_ok = get_local_sensor_snapshot(&temperature,
                                               &humidity,
                                               &temperature_trend,
                                               &humidity_trend);
    if (sensor_ok) {
        ui_text::format_or_fallback(temp_text, sizeof(temp_text), kFlipTempPlaceholder, kFlipTempFormat, temperature);
        ui_text::format_or_fallback(humi_text, sizeof(humi_text), kFlipHumiPlaceholder, kFlipHumiFormat, humidity);
    } else {
        strlcpy(temp_text, kFlipTempPlaceholder, sizeof(temp_text));
        strlcpy(humi_text, kFlipHumiPlaceholder, sizeof(humi_text));
    }
    int temp_mood = sensor_ok ? temperature_mood(temperature) : kSensorMoodUnavailable;
    int humi_mood = sensor_ok ? humidity_mood(humidity) : kSensorMoodUnavailable;
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
                                      sensor_ok ? temperature_trend : 0,
                                      &g_last_flip_temp_trend);
    changed |= update_flip_trend_icon(g_flip_clock_humi_trend_canvas,
                                      sensor_ok ? humidity_trend : 0,
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
    ui_text::format_or_fallback(day_text, sizeof(day_text), kFlipDayPlaceholder, kFlipDayFormat, local.tm_mday);
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

void build_flip_sensor_value_labels(lv_obj_t *parent,
                                    lv_obj_t **normal_label,
                                    lv_obj_t **bold_x_label,
                                    lv_obj_t **bold_y_label,
                                    int y,
                                    const char *placeholder,
                                    const char *failure_log)
{
    if (!normal_label || !bold_x_label || !bold_y_label) {
        return;
    }
    const int x = kFlipSensorPanelX + kFlipSensorTextPadX;
    *normal_label = make_flip_sensor_value_label(parent,
                                                  x,
                                                  y,
                                                  placeholder,
                                                  LV_TEXT_ALIGN_LEFT);
    *bold_x_label = make_flip_sensor_value_label(parent,
                                                  x + kFlipSensorBoldOffset,
                                                  y,
                                                  placeholder,
                                                  LV_TEXT_ALIGN_LEFT);
    *bold_y_label = make_flip_sensor_value_label(parent,
                                                  x,
                                                  y + kFlipSensorBoldYOffset,
                                                  placeholder,
                                                  LV_TEXT_ALIGN_LEFT);
    if (!*normal_label || !*bold_x_label || !*bold_y_label) {
        ESP_LOGW(TAG, "%s", failure_log ? failure_log : FLIP_CLOCK_TEMP_LABEL_CREATE_FAILED_LOG);
    }
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

void build_flip_date_label_group(lv_obj_t *parent,
                                 lv_obj_t **normal_label,
                                 lv_obj_t **bold_x_label,
                                 lv_obj_t **bold_y_label,
                                 lv_obj_t **bold_xy_label,
                                 int y,
                                 int height,
                                 const char *text,
                                 const lv_font_t *font)
{
    if (!normal_label || !bold_x_label || !bold_y_label) {
        return;
    }
    *normal_label = make_flip_date_label(parent,
                                         kFlipDatePanelX,
                                         y,
                                         kFlipDatePanelW,
                                         height,
                                         text,
                                         font);
    *bold_x_label = make_flip_date_label(parent,
                                         kFlipDatePanelX + kFlipDateBoldOffset,
                                         y,
                                         kFlipDatePanelW,
                                         height,
                                         text,
                                         font);
    *bold_y_label = make_flip_date_label(parent,
                                         kFlipDatePanelX,
                                         y + kFlipDateBoldYOffset,
                                         kFlipDatePanelW,
                                         height,
                                         text,
                                         font);
    if (bold_xy_label) {
        *bold_xy_label = make_flip_date_label(parent,
                                              kFlipDatePanelX + kFlipDateBoldOffset,
                                              y + kFlipDateBoldYOffset,
                                              kFlipDatePanelW,
                                              height,
                                              text,
                                              font);
    }
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
    if (!*buffer) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, FLIP_CLOCK_MOOD_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    configure_canvas_base(*canvas,
                          *buffer,
                          kFlipMoodCanvasX,
                          y,
                          FLIP_SENSOR_ICON_WIDTH,
                          FLIP_SENSOR_ICON_HEIGHT);
    lv_canvas_fill_bg(*canvas, lv_color_black(), LV_OPA_COVER);
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
    if (!*buffer) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, FLIP_CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    configure_canvas_base(*canvas,
                          *buffer,
                          kFlipTrendCanvasX,
                          y,
                          kFlipTrendCanvasW,
                          kFlipTrendCanvasH);
    update_flip_trend_icon(*canvas, trend, nullptr);
}

void build_flip_sensor_panel(lv_obj_t *screen)
{
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

    build_flip_sensor_value_labels(screen,
                                   &g_flip_clock_sensor_label,
                                   &g_flip_clock_sensor_bold_label,
                                   &g_flip_clock_sensor_bold_y_label,
                                   kFlipSensorTempTextY,
                                   kFlipTempPlaceholder,
                                   FLIP_CLOCK_TEMP_LABEL_CREATE_FAILED_LOG);
    build_flip_sensor_value_labels(screen,
                                   &g_flip_clock_humidity_label,
                                   &g_flip_clock_humidity_bold_label,
                                   &g_flip_clock_humidity_bold_y_label,
                                   kFlipSensorHumiTextY,
                                   kFlipHumiPlaceholder,
                                   FLIP_CLOCK_HUMIDITY_LABEL_CREATE_FAILED_LOG);
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

    int initial_temperature_trend = 0;
    int initial_humidity_trend = 0;
    bool initial_sensor_ok = get_local_sensor_snapshot(nullptr,
                                                       nullptr,
                                                       &initial_temperature_trend,
                                                       &initial_humidity_trend);
    build_flip_trend_canvas(screen,
                            &g_flip_clock_temp_trend_canvas,
                            &g_flip_clock_temp_trend_canvas_buf,
                            kFlipTempTrendCanvasY,
                            initial_sensor_ok ? initial_temperature_trend : 0,
                            "temperature");
    build_flip_trend_canvas(screen,
                            &g_flip_clock_humi_trend_canvas,
                            &g_flip_clock_humi_trend_canvas_buf,
                            kFlipHumiTrendCanvasY,
                            initial_sensor_ok ? initial_humidity_trend : 0,
                            "humidity");
}

void build_flip_date_panel(lv_obj_t *screen)
{
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
    build_flip_date_label_group(screen,
                                &g_flip_clock_day_label,
                                &g_flip_clock_day_bold_label,
                                &g_flip_clock_day_bold_y_label,
                                nullptr,
                                kFlipDayTextY,
                                kFlipDayTextH,
                                kFlipDayPlaceholder,
                                &lv_font_montserrat_48);
    build_flip_date_label_group(screen,
                                &g_flip_clock_lunar_label,
                                &g_flip_clock_lunar_bold_x_label,
                                &g_flip_clock_lunar_bold_y_label,
                                &g_flip_clock_lunar_bold_xy_label,
                                kFlipLunarTextY,
                                kFlipLunarTextH,
                                kFlipDayPlaceholder,
                                &zh_flip_lunar_22);
}

void reset_flip_clock_refresh_cache()
{
    g_last_flip_clock_hour = -1;
    g_last_flip_clock_minute = -1;
    g_last_flip_clock_second = -1;
    g_last_flip_sensor_minute = -1;
    g_last_flip_temp_mood = -1;
    g_last_flip_humi_mood = -1;
    g_last_flip_temp_trend = 99;
    g_last_flip_humi_trend = 99;
    g_last_flip_date_key = -1;
}

} // namespace

bool update_flip_clock_sensor_status()
{
    return update_flip_sensor_text();
}

void build_inverted_clock_cards(lv_obj_t *parent,
                                lv_obj_t *card_canvas[3],
                                lv_color_t *card_canvas_buf[3])
{
    if (!parent || !card_canvas || !card_canvas_buf) {
        return;
    }
    for (int i = 0; i < kCardCount; ++i) {
        if (!card_canvas_buf[i]) {
            card_canvas_buf[i] = alloc_canvas_buffer(kCardW, kCardH);
        }
        if (!card_canvas_buf[i]) {
            continue;
        }
        card_canvas[i] = lv_canvas_create(parent);
        if (!card_canvas[i]) {
            ESP_LOGW(TAG, FLIP_CLOCK_CARD_CANVAS_CREATE_FAILED_FORMAT, i);
            continue;
        }
        configure_canvas_base(card_canvas[i],
                              card_canvas_buf[i],
                              kCardX[i],
                              kCardY,
                              kCardW,
                              kCardH);
        lv_canvas_fill_bg(card_canvas[i], lv_color_black(), LV_OPA_COVER);
    }
}

bool update_inverted_clock_cards(const struct tm &local,
                                 lv_obj_t *card_canvas[3],
                                 int last_values[3])
{
    if (!card_canvas || !last_values) {
        return false;
    }
    const int values[kCardCount] = {local.tm_hour, local.tm_min, local.tm_sec};
    bool changed = false;
    for (int i = 0; i < kCardCount; ++i) {
        changed |= update_inverted_clock_card_value(card_canvas[i],
                                                     values[i],
                                                     &last_values[i]);
    }
    return changed;
}

void clear_inverted_clock_card(lv_obj_t *card_canvas)
{
    if (!card_canvas) {
        return;
    }
    draw_card_shell(card_canvas);
    apply_card_rounding(card_canvas);
    lv_obj_invalidate(card_canvas);
}

bool update_inverted_clock_card_value(lv_obj_t *card_canvas,
                                      int value,
                                      int *last_value)
{
    if (!card_canvas || !last_value || value < 0 || value > 99 || value == *last_value) {
        return false;
    }
    *last_value = value;
    draw_flip_card(card_canvas, value);
    return true;
}

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
                               nullptr,
                               false);

    lv_obj_t *top_line = make_bar(screen,
                                  kFlipTopLineX,
                                  kFlipTopLineY,
                                  kFlipTopLineW,
                                  kFlipTopLineH);
    set_obj_black(top_line, true);
    build_work_page_day_progress(screen, kWorkPageFlipClock);

    build_inverted_clock_cards(screen,
                               g_flip_clock_card_canvas,
                               g_flip_clock_card_canvas_buf);
    build_flip_sensor_panel(screen);
    build_flip_date_panel(screen);

    update_battery_segments(g_flip_clock_battery_segments, g_battery_percent);
    reset_flip_clock_refresh_cache();
}

bool update_flip_clock_page(const struct tm &local)
{
    build_flip_clock_page();
    if (!g_flip_clock_root) {
        return false;
    }
    int last_values[kCardCount] = {
        g_last_flip_clock_hour,
        g_last_flip_clock_minute,
        g_last_flip_clock_second,
    };
    bool changed = update_inverted_clock_cards(local,
                                                g_flip_clock_card_canvas,
                                                last_values);
    g_last_flip_clock_hour = last_values[kHourCardIndex];
    g_last_flip_clock_minute = last_values[kMinuteCardIndex];
    g_last_flip_clock_second = last_values[kSecondCardIndex];
    ClockUiTimeSnapshot time_snapshot = clock_ui_time_snapshot(local);
    int minute = local.tm_min;
    if (time_snapshot.date_key != g_last_flip_date_key) {
        g_last_flip_date_key = time_snapshot.date_key;
        changed |= update_flip_date_text(local);
    }

    if (minute != g_last_flip_sensor_minute) {
        g_last_flip_sensor_minute = minute;
        changed |= update_flip_sensor_text();
    }
    return changed;
}
