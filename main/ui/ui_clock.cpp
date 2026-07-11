// 构建和刷新天气时钟主页的时间、天气、温湿度和状态区域。
#include "ui_views.h"

#include "audio_services.h"
#include "sensor_services.h"
#include "ui_battery.h"
#include "ui_setup_status.h"
#include "ui_text_format.h"

namespace {
template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr size_t kClockDateTextSize = 48;
constexpr const char *kClockDateFormat = "%04d/%02d/%02d / %s";
constexpr const char *kClockDatePlaceholder = "--";
#define CLOCK_DATE_LABEL_CREATE_FAILED_LOG "clock date label create failed"
#define CLOCK_ALERT_PILL_CREATE_FAILED_LOG "clock alert pill create failed"
#define CLOCK_ALERT_ICON_CANVAS_CREATE_FAILED_LOG "clock alert icon canvas create failed"
#define CLOCK_ALERT_LABEL_CREATE_FAILED_LOG "clock alert label create failed"
#define CLOCK_STATUS_ICON_CANVAS_CREATE_FAILED_FORMAT "clock status icon canvas create failed x=%d"
#define CLOCK_LABEL_CREATE_FAILED_FORMAT "clock label create failed name=%s"
#define CLOCK_ICON_CANVAS_CREATE_FAILED_FORMAT "clock icon canvas create failed name=%s"
#define CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT "clock trend canvas create failed name=%s"
#define CLOCK_FILL_CANVAS_CREATE_FAILED_FORMAT "clock fill canvas create failed name=%s"
constexpr const char *kClockComponentFallbackName = "component";
constexpr const char *kClockComponentWeatherCity = "weather_city";
constexpr const char *kClockComponentWeatherIcon = "weather_icon";
constexpr const char *kClockComponentWeatherInfo = "weather_info";
constexpr const char *kClockComponentWeatherTemp = "weather_temp";
constexpr const char *kClockComponentWeatherHumidity = "weather_humi";
constexpr const char *kClockComponentTempIcon = "temp_icon";
constexpr const char *kClockComponentHumidityIcon = "humi_icon";
constexpr const char *kClockComponentTempValue = "temp_value";
constexpr const char *kClockComponentHumidityValue = "humi_value";
constexpr const char *kClockComponentTempTrend = "temp_trend";
constexpr const char *kClockComponentHumidityTrend = "humi_trend";
constexpr const char *kClockComponentTime = "time";
constexpr const char *kClockComponentSecond = "second";
constexpr const char *kClockComponentStatusGif = "status_gif";
constexpr const char *kClockComponentLowBatteryIcon = "low_battery_icon";
constexpr const char *kClockLogTexts[] = {
    CLOCK_DATE_LABEL_CREATE_FAILED_LOG,
    CLOCK_ALERT_PILL_CREATE_FAILED_LOG,
    CLOCK_ALERT_ICON_CANVAS_CREATE_FAILED_LOG,
    CLOCK_ALERT_LABEL_CREATE_FAILED_LOG,
    CLOCK_STATUS_ICON_CANVAS_CREATE_FAILED_FORMAT,
    CLOCK_LABEL_CREATE_FAILED_FORMAT,
    CLOCK_ICON_CANVAS_CREATE_FAILED_FORMAT,
    CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT,
    CLOCK_FILL_CANVAS_CREATE_FAILED_FORMAT,
    kClockComponentFallbackName,
    kClockComponentWeatherCity,
    kClockComponentWeatherIcon,
    kClockComponentWeatherInfo,
    kClockComponentWeatherTemp,
    kClockComponentWeatherHumidity,
    kClockComponentTempIcon,
    kClockComponentHumidityIcon,
    kClockComponentTempValue,
    kClockComponentHumidityValue,
    kClockComponentTempTrend,
    kClockComponentHumidityTrend,
    kClockComponentTime,
    kClockComponentSecond,
    kClockComponentStatusGif,
    kClockComponentLowBatteryIcon,
};
constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (size_t i = 0; i < N; ++i) {
        if (!cstr_nonempty(items[i])) {
            return false;
        }
    }
    return true;
}

static_assert(array_count(kClockLogTexts) > 0, "clock log registry must not be empty");
static_assert(cstr_array_nonempty(kClockLogTexts), "clock log texts must be non-empty");
constexpr int kTmYearOffset = 1900;
constexpr int kTmMonthOffset = 1;
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr int kProgressSegmentCount = 60;
constexpr int kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
constexpr int kSecondsPerDay = kHoursPerDay * kSecondsPerHour;
constexpr int kClockTimeCanvasX = 18;
constexpr int kClockTimeCanvasY = 76;
constexpr int kClockTimeCanvasWidth = 292;
constexpr int kClockTimeCanvasHeight = 92;
constexpr int kClockSecondCanvasX = 320;
constexpr int kClockSecondCanvasY = 124;
constexpr int kClockSecondCanvasWidth = 60;
constexpr int kClockSecondCanvasHeight = 40;
constexpr int kClockStatusGifCanvasX = 279;
constexpr int kClockStatusGifCanvasY = 196;
constexpr int kClockDateLabelX = 198;
constexpr int kClockDateLabelY = 15;
constexpr int kClockDateLabelWidth = 182;
constexpr int kClockDateLabelHeight = 26;
constexpr int kClockAlertPillX = 64;
constexpr int kClockAlertPillY = 11;
constexpr int kClockAlertPillWidth = 128;
constexpr int kClockAlertPillHeight = 26;
constexpr int kClockAlertPillRadius = 13;
constexpr int kClockAlertIconX = 4;
constexpr int kClockAlertIconY = 4;
constexpr int kClockAlertLabelX = 24;
constexpr int kClockAlertLabelY = 4;
constexpr int kClockAlertLabelWidth = 94;
constexpr int kClockAlertLabelHeight = 18;
constexpr int kClockChimeStatusIconX = 64;
constexpr int kClockChimeStatusIconY = 15;
constexpr int kClockWifiStatusIconX = 90;
constexpr int kClockWifiStatusIconY = 15;
constexpr int kClockAlarmStatusIconX = 116;
constexpr int kClockAlarmStatusIconY = 15;
constexpr int kClockDividerX = 18;
constexpr int kClockTopDividerY = 54;
constexpr int kClockBottomDividerY = 184;
constexpr int kClockDividerWidth = 364;
constexpr int kClockDividerHeight = 4;
constexpr int kClockDayProgressCanvasY = 59;
constexpr int kClockSecondProgressCanvasY = 180;
constexpr int kClockLowerPanelSeparatorY = 188;
constexpr int kClockLowerPanelSeparatorWidth = 2;
constexpr int kClockLowerPanelSeparatorHeight = 102;
constexpr int kClockLowerPanelSeparatorAX = 139;
constexpr int kClockLowerPanelSeparatorBX = 260;
constexpr int kClockWeatherCityLabelX = 14;
constexpr int kClockWeatherCityLabelY = 196;
constexpr int kClockWeatherCityLabelWidth = 76;
constexpr int kClockWeatherCityLabelHeight = 20;
constexpr int kClockWeatherIconLabelX = 91;
constexpr int kClockWeatherIconLabelY = 194;
constexpr int kClockWeatherIconLabelWidth = 34;
constexpr int kClockWeatherIconLabelHeight = 38;
constexpr int kClockWeatherInfoLabelX = 14;
constexpr int kClockWeatherInfoLabelY = 218;
constexpr int kClockWeatherInfoLabelWidth = 76;
constexpr int kClockWeatherInfoLabelHeight = 20;
constexpr int kClockWeatherMetricLabelX = 20;
constexpr int kClockWeatherTempLabelY = 242;
constexpr int kClockWeatherHumiLabelY = 264;
constexpr int kClockWeatherMetricLabelWidth = 68;
constexpr int kClockWeatherMetricLabelHeight = 20;
constexpr int kClockTempIconX = 152;
constexpr int kClockTempIconY = 214;
constexpr int kClockHumiIconX = 154;
constexpr int kClockHumiIconY = 244;
constexpr int kClockLocalMetricLabelX = 174;
constexpr int kClockLocalTempLabelY = 214;
constexpr int kClockLocalHumiLabelY = 246;
constexpr int kClockLocalMetricLabelWidth = 62;
constexpr int kClockLocalMetricLabelHeight = 28;
constexpr int kClockTrendCanvasX = 239;
constexpr int kClockTempTrendCanvasY = 215;
constexpr int kClockHumiTrendCanvasY = 248;
constexpr int kClockLowBatteryIconX = 156;
constexpr int kClockLowBatteryIconY = 214;

const char *clock_component_name(const char *name)
{
    return cstr_nonempty(name) ? name : kClockComponentFallbackName;
}

bool prepare_clock_canvas_buffer(lv_color_t **buffer, int width, int height)
{
    if (!buffer) {
        return false;
    }
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(width, height);
    }
    return *buffer != nullptr;
}

void configure_clock_canvas(lv_obj_t *canvas, int x, int y, int width, int height)
{
    if (!canvas) {
        return;
    }
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(canvas, x, y);
    lv_obj_set_size(canvas, width, height);
    lv_obj_set_style_border_width(canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(canvas, 0, LV_PART_MAIN);
}

void configure_clock_alert_pill(lv_obj_t *pill)
{
    if (!pill) {
        return;
    }
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(pill, kClockAlertPillX, kClockAlertPillY);
    lv_obj_set_size(pill, kClockAlertPillWidth, kClockAlertPillHeight);
    lv_obj_set_style_bg_color(pill, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, kClockAlertPillRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
}

void build_clock_status_icon(lv_obj_t *screen,
                             lv_obj_t **canvas,
                             lv_color_t **buffer,
                             int x,
                             int y,
                             int width,
                             int height,
                             int bytes_per_row,
                             const uint8_t *bits)
{
    if (!screen || !canvas || !buffer || !bits) {
        return;
    }
    if (!prepare_clock_canvas_buffer(buffer, width, height)) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_STATUS_ICON_CANVAS_CREATE_FAILED_FORMAT, x);
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(*canvas, width, height, bytes_per_row, bits, lv_color_black(), lv_color_white());
    lv_obj_add_flag(*canvas, LV_OBJ_FLAG_HIDDEN);
}

void center_clock_label_if_created(lv_obj_t *label, const char *name)
{
    if (!label) {
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, clock_component_name(name));
        return;
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

lv_obj_t *make_clock_lower_center_label(lv_obj_t *screen,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        const char *text,
                                        const char *name)
{
    lv_obj_t *label = make_label(screen, x, y, width, height, text);
    remember_lower_panel_object(label);
    center_clock_label_if_created(label, name);
    return label;
}

void build_clock_lower_icon(lv_obj_t *screen,
                            lv_obj_t **canvas,
                            lv_color_t **buffer,
                            int x,
                            int y,
                            int width,
                            int height,
                            int bytes_per_row,
                            const uint8_t *bits,
                            const char *name)
{
    if (!screen || !canvas || !buffer || !bits) {
        return;
    }
    if (!prepare_clock_canvas_buffer(buffer, width, height)) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_ICON_CANVAS_CREATE_FAILED_FORMAT, clock_component_name(name));
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(*canvas, width, height, bytes_per_row, bits, lv_color_black(), lv_color_white());
}

void build_clock_trend_canvas(lv_obj_t *screen,
                              lv_obj_t **canvas,
                              lv_color_t **buffer,
                              int x,
                              int y,
                              int trend,
                              const char *name)
{
    if (!screen || !canvas || !buffer) {
        return;
    }
    if (!prepare_clock_canvas_buffer(buffer, TREND_ICON_WIDTH, TREND_ICON_HEIGHT)) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT, clock_component_name(name));
        return;
    }
    configure_clock_canvas(*canvas, x, y, TREND_ICON_WIDTH, TREND_ICON_HEIGHT);
    lv_canvas_set_buffer(*canvas, *buffer, TREND_ICON_WIDTH, TREND_ICON_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    update_trend_icon(*canvas, trend, nullptr);
}

void build_clock_fill_canvas(lv_obj_t *screen,
                             lv_obj_t **canvas,
                             lv_color_t **buffer,
                             int x,
                             int y,
                             int width,
                             int height,
                             const char *name)
{
    if (!screen || !canvas || !buffer) {
        return;
    }
    if (!prepare_clock_canvas_buffer(buffer, width, height)) {
        return;
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_FILL_CANVAS_CREATE_FAILED_FORMAT, clock_component_name(name));
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(*canvas, lv_color_white(), LV_OPA_COVER);
}
} // namespace

void build_clock_ui()
{
    if (g_clock_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_clock_root = screen;

    g_date_label = make_label(screen,
                              kClockDateLabelX,
                              kClockDateLabelY,
                              kClockDateLabelWidth,
                              kClockDateLabelHeight,
                              "----/--/-- / 星期-");
    if (g_date_label) {
        lv_obj_set_style_text_align(g_date_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    } else {
        ESP_LOGW(TAG, "%s", CLOCK_DATE_LABEL_CREATE_FAILED_LOG);
    }
    build_battery_icon(screen, g_battery_segments);

    g_alert_pill = lv_obj_create(screen);
    if (g_alert_pill) {
        configure_clock_alert_pill(g_alert_pill);

        if (prepare_clock_canvas_buffer(&g_alert_icon_canvas_buf, WARNING_ICON_WIDTH, WARNING_ICON_HEIGHT)) {
            g_alert_icon_canvas = lv_canvas_create(g_alert_pill);
            if (g_alert_icon_canvas) {
                configure_clock_canvas(g_alert_icon_canvas,
                                       kClockAlertIconX,
                                       kClockAlertIconY,
                                       WARNING_ICON_WIDTH,
                                       WARNING_ICON_HEIGHT);
                lv_canvas_set_buffer(g_alert_icon_canvas,
                                     g_alert_icon_canvas_buf,
                                     WARNING_ICON_WIDTH,
                                     WARNING_ICON_HEIGHT,
                                     LV_IMG_CF_TRUE_COLOR);
                draw_1bit_icon(g_alert_icon_canvas,
                               WARNING_ICON_WIDTH,
                               WARNING_ICON_HEIGHT,
                               WARNING_ICON_BYTES_PER_ROW,
                               warning_icon_bits,
                               lv_color_white(),
                               lv_color_black());
            } else {
                ESP_LOGW(TAG, "%s", CLOCK_ALERT_ICON_CANVAS_CREATE_FAILED_LOG);
            }
        }
        g_alert_label = make_label_with_font(g_alert_pill,
                                             kClockAlertLabelX,
                                             kClockAlertLabelY,
                                             kClockAlertLabelWidth,
                                             kClockAlertLabelHeight,
                                             "",
                                             &zh_font_16);
        if (g_alert_label) {
            lv_obj_set_style_text_color(g_alert_label, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(g_alert_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_label_set_long_mode(g_alert_label, LV_LABEL_LONG_CLIP);
        } else {
            ESP_LOGW(TAG, "%s", CLOCK_ALERT_LABEL_CREATE_FAILED_LOG);
        }
    } else {
        ESP_LOGW(TAG, "%s", CLOCK_ALERT_PILL_CREATE_FAILED_LOG);
    }

    build_clock_status_icon(screen,
                            &g_chime_status_icon_canvas,
                            &g_chime_status_icon_canvas_buf,
                            kClockChimeStatusIconX,
                            kClockChimeStatusIconY,
                            CHIME_STATUS_ICON_WIDTH,
                            CHIME_STATUS_ICON_HEIGHT,
                            CHIME_STATUS_ICON_BYTES_PER_ROW,
                            chime_status_icon_bits);

    build_clock_status_icon(screen,
                            &g_wifi_status_icon_canvas,
                            &g_wifi_status_icon_canvas_buf,
                            kClockWifiStatusIconX,
                            kClockWifiStatusIconY,
                            WIFI_STATUS_ICON_WIDTH,
                            WIFI_STATUS_ICON_HEIGHT,
                            WIFI_STATUS_ICON_BYTES_PER_ROW,
                            wifi_status_icon_bits);

    build_clock_status_icon(screen,
                            &g_alarm_status_icon_canvas,
                            &g_alarm_status_icon_canvas_buf,
                            kClockAlarmStatusIconX,
                            kClockAlarmStatusIconY,
                            ALARM_STATUS_ICON_WIDTH,
                            ALARM_STATUS_ICON_HEIGHT,
                            ALARM_STATUS_ICON_BYTES_PER_ROW,
                            alarm_status_icon_bits);

    g_weather_city_label = make_clock_lower_center_label(screen,
                                                         kClockWeatherCityLabelX,
                                                         kClockWeatherCityLabelY,
                                                         kClockWeatherCityLabelWidth,
                                                         kClockWeatherCityLabelHeight,
                                                         kClockWeatherCityPlaceholder,
                                                         kClockComponentWeatherCity);
    g_weather_icon_label = make_label(screen,
                                      kClockWeatherIconLabelX,
                                      kClockWeatherIconLabelY,
                                      kClockWeatherIconLabelWidth,
                                      kClockWeatherIconLabelHeight,
                                      "");
    remember_lower_panel_object(g_weather_icon_label);
    if (g_weather_icon_label) {
        lv_obj_set_style_text_font(g_weather_icon_label, &qweather_icons_36, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_weather_icon_label, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_weather_icon_label, 0, LV_PART_MAIN);
        lv_obj_set_style_text_align(g_weather_icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else {
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, kClockComponentWeatherIcon);
    }
    g_weather_info_label = make_label(screen,
                                      kClockWeatherInfoLabelX,
                                      kClockWeatherInfoLabelY,
                                      kClockWeatherInfoLabelWidth,
                                      kClockWeatherInfoLabelHeight,
                                      kClockWeatherInfoWaitingText);
    remember_lower_panel_object(g_weather_info_label);
    if (g_weather_info_label) {
        lv_label_set_long_mode(g_weather_info_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(g_weather_info_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else {
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, kClockComponentWeatherInfo);
    }
    g_weather_temp_label = make_clock_lower_center_label(screen,
                                                         kClockWeatherMetricLabelX,
                                                         kClockWeatherTempLabelY,
                                                         kClockWeatherMetricLabelWidth,
                                                         kClockWeatherMetricLabelHeight,
                                                         kClockWeatherTempPlaceholder,
                                                         kClockComponentWeatherTemp);
    g_weather_humi_label = make_clock_lower_center_label(screen,
                                                         kClockWeatherMetricLabelX,
                                                         kClockWeatherHumiLabelY,
                                                         kClockWeatherMetricLabelWidth,
                                                         kClockWeatherMetricLabelHeight,
                                                         kClockWeatherHumidityPlaceholder,
                                                         kClockComponentWeatherHumidity);

    build_clock_lower_icon(screen,
                           &g_temp_icon_canvas,
                           &g_temp_icon_canvas_buf,
                           kClockTempIconX,
                           kClockTempIconY,
                           TEMP_ICON_WIDTH,
                           TEMP_ICON_HEIGHT,
                           TEMP_ICON_BYTES_PER_ROW,
                           temp_icon_bits,
                           kClockComponentTempIcon);
    build_clock_lower_icon(screen,
                           &g_humi_icon_canvas,
                           &g_humi_icon_canvas_buf,
                           kClockHumiIconX,
                           kClockHumiIconY,
                           HUMI_ICON_WIDTH,
                           HUMI_ICON_HEIGHT,
                           HUMI_ICON_BYTES_PER_ROW,
                           humi_icon_bits,
                           kClockComponentHumidityIcon);
    g_temp_label = make_clock_lower_center_label(screen,
                                                 kClockLocalMetricLabelX,
                                                 kClockLocalTempLabelY,
                                                 kClockLocalMetricLabelWidth,
                                                 kClockLocalMetricLabelHeight,
                                                 "--.-℃",
                                                 kClockComponentTempValue);
    g_humi_label = make_clock_lower_center_label(screen,
                                                 kClockLocalMetricLabelX,
                                                 kClockLocalHumiLabelY,
                                                 kClockLocalMetricLabelWidth,
                                                 kClockLocalMetricLabelHeight,
                                                 "--.-%",
                                                 kClockComponentHumidityValue);
    remember_lower_panel_object(g_temp_icon_canvas);
    remember_lower_panel_object(g_humi_icon_canvas);
    int initial_temperature_trend = 0;
    int initial_humidity_trend = 0;
    bool initial_sensor_ok = get_local_sensor_snapshot(nullptr,
                                                       nullptr,
                                                       &initial_temperature_trend,
                                                       &initial_humidity_trend);
    build_clock_trend_canvas(screen,
                             &g_temp_trend_canvas,
                             &g_temp_trend_canvas_buf,
                             kClockTrendCanvasX,
                             kClockTempTrendCanvasY,
                             initial_sensor_ok ? initial_temperature_trend : 0,
                             kClockComponentTempTrend);
    build_clock_trend_canvas(screen,
                             &g_humi_trend_canvas,
                             &g_humi_trend_canvas_buf,
                             kClockTrendCanvasX,
                             kClockHumiTrendCanvasY,
                             initial_sensor_ok ? initial_humidity_trend : 0,
                             kClockComponentHumidityTrend);
    remember_lower_panel_object(g_temp_trend_canvas);
    remember_lower_panel_object(g_humi_trend_canvas);
    build_clock_fill_canvas(screen,
                            &g_time_canvas,
                            &g_time_canvas_buf,
                            kClockTimeCanvasX,
                            kClockTimeCanvasY,
                            kClockTimeCanvasWidth,
                            kClockTimeCanvasHeight,
                            kClockComponentTime);

    build_clock_fill_canvas(screen,
                            &g_second_canvas,
                            &g_second_canvas_buf,
                            kClockSecondCanvasX,
                            kClockSecondCanvasY,
                            kClockSecondCanvasWidth,
                            kClockSecondCanvasHeight,
                            kClockComponentSecond);

    build_clock_fill_canvas(screen,
                            &g_status_gif_canvas,
                            &g_status_gif_canvas_buf,
                            kClockStatusGifCanvasX,
                            kClockStatusGifCanvasY,
                            STATUS_GIF_WIDTH,
                            STATUS_GIF_HEIGHT,
                            kClockComponentStatusGif);
    remember_lower_panel_object(g_status_gif_canvas);
    if (g_status_gif_canvas_buf) {
        draw_status_gif_frame(0);
    }

    lv_obj_t *top_line = make_bar(screen, kClockDividerX, kClockTopDividerY, kClockDividerWidth, kClockDividerHeight);
    lv_obj_t *bottom_line =
        make_bar(screen, kClockDividerX, kClockBottomDividerY, kClockDividerWidth, kClockDividerHeight);
    build_progress_canvas(screen, &g_day_progress_canvas, &g_day_progress_canvas_buf, kClockDayProgressCanvasY);
    build_progress_canvas(screen, &g_second_progress_canvas, &g_second_progress_canvas_buf, kClockSecondProgressCanvasY);
    g_panel_sep_a = make_bar(screen,
                             kClockLowerPanelSeparatorAX,
                             kClockLowerPanelSeparatorY,
                             kClockLowerPanelSeparatorWidth,
                             kClockLowerPanelSeparatorHeight);
    g_panel_sep_b = make_bar(screen,
                             kClockLowerPanelSeparatorBX,
                             kClockLowerPanelSeparatorY,
                             kClockLowerPanelSeparatorWidth,
                             kClockLowerPanelSeparatorHeight);
    set_obj_black(top_line, true);
    set_obj_black(bottom_line, true);
    set_obj_black(g_panel_sep_a, true);
    set_obj_black(g_panel_sep_b, true);

    build_clock_lower_icon(screen,
                           &g_low_battery_icon_canvas,
                           &g_low_battery_icon_canvas_buf,
                           kClockLowBatteryIconX,
                           kClockLowBatteryIconY,
                           LOW_BATTERY_ICON_WIDTH,
                           LOW_BATTERY_ICON_HEIGHT,
                           LOW_BATTERY_ICON_BYTES_PER_ROW,
                           low_battery_icon_bits,
                           kClockComponentLowBatteryIcon);
    if (g_low_battery_icon_canvas) {
        lv_obj_add_flag(g_low_battery_icon_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    build_setup_status_panel(screen);
}

void format_clock_date_text(char *out, size_t out_len, const struct tm &local, const char *weekday)
{
    if (!ui_text::output_buffer_available(out, out_len)) {
        return;
    }
    int written = snprintf(out,
                           out_len,
                           kClockDateFormat,
                           local.tm_year + kTmYearOffset,
                           local.tm_mon + kTmMonthOffset,
                           local.tm_mday,
                           weekday ? weekday : kClockDatePlaceholder);
    if (ui_text::format_failed(written, out_len)) {
        strlcpy(out, kClockDatePlaceholder, out_len);
    }
}

lv_obj_t *work_page_date_label(int page)
{
    switch (page) {
    case kWorkPageWeatherClock:
        return g_date_label;
    case kWorkPageHistory:
        return g_history_date_label;
    case kWorkPageGallery:
        return g_gallery_date_label;
    case kWorkPageCalendar:
        return g_calendar_date_label;
    case kWorkPageWeatherBoard:
        return g_weather_board_date_label;
    case kWorkPageFlipClock:
        return g_flip_clock_date_label;
    case kWorkPageXiaozhiAI:
        return g_xiaozhi_date_label;
    default:
        return nullptr;
    }
}

bool update_time_ui(const struct tm &local, bool clock_page_active, int active_work_page)
{
    bool changed = false;
    static int last_chime_hour_key = -1;
    int minute_key = local.tm_hour * 60 + local.tm_min;
    if (clock_page_active && minute_key != g_last_ui_minute) {
        draw_time_canvas(local);
        if (!g_low_battery_mode) {
            int day_seconds = local.tm_hour * kSecondsPerHour + local.tm_min * kSecondsPerMinute + local.tm_sec;
            int day_filled = (day_seconds * kProgressSegmentCount) / kSecondsPerDay;
            update_progress_canvas(g_day_progress_canvas, day_filled, &g_last_day_progress_filled);
        }
        g_last_ui_minute = minute_key;
        changed = true;
    }
    if (clock_page_active && !g_low_battery_mode && local.tm_sec != g_last_ui_second) {
        draw_second_canvas(local);
        draw_status_gif_frame(local.tm_sec % STATUS_GIF_FRAME_COUNT);
        update_progress_canvas(g_second_progress_canvas, local.tm_sec + 1, &g_last_second_progress_filled);
        g_last_ui_second = local.tm_sec;
        changed = true;
    }

    int date_key = (local.tm_year + kTmYearOffset) * 10000 + (local.tm_mon + kTmMonthOffset) * 100 + local.tm_mday;
    int date_page = (active_work_page == kWorkPageWeatherClock || g_low_battery_mode || g_setup_portal_active)
                        ? kWorkPageWeatherClock
                        : active_work_page;
    if (date_key != g_last_ui_date_key || date_page != g_last_ui_date_page) {
        static const char *kWeekdayNames[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
        static_assert(array_count(kWeekdayNames) == 7, "tm_wday maps to exactly seven weekday names");
        char date[kClockDateTextSize] = {};
        format_clock_date_text(date, sizeof(date), local, kWeekdayNames[local.tm_wday]);
        changed |= set_label_text_if_changed(work_page_date_label(date_page), date);
        g_last_ui_date_key = date_key;
        g_last_ui_date_page = date_page;
    }

    int hour_key = local.tm_yday * 24 + local.tm_hour;
    bool chime_enabled = g_hourly_chime_enabled || g_hourly_chime_all_day;
    if (chime_enabled && !g_low_battery_mode &&
        local.tm_min == 0 && local.tm_sec <= 2 && hour_key != last_chime_hour_key) {
        last_chime_hour_key = hour_key;
        play_hourly_chime(local.tm_hour);
    }
    return changed;
}
