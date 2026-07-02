// 构建和刷新天气时钟主页的时间、天气、温湿度和状态区域。
#include "ui_views.h"

#include "audio_services.h"
#include "network_services.h"
#include "ota_services.h"
#include "sensor_services.h"

namespace {
constexpr int kChimeVolumeLevels[] = {20, 40, 60, 80, 100};
template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr int kChimeVolumeLevelCount = static_cast<int>(array_count(kChimeVolumeLevels));
constexpr int kDefaultChimeVolumePercent = kChimeVolumeLevels[0];
constexpr int kSettingsFeedbackDefaultMs = 2500;
constexpr int kSettingsFeedbackBusyMs = 2000;
constexpr int kSettingsFeedbackSavedMs = 1800;
constexpr int kSettingsFeedbackInstructionMs = 3500;
constexpr const char *kSettingsSaveFailedFeedback = "保存失败";
constexpr const char *kSettingsOrderSavedFeedback = "页面顺序已保存";
constexpr const char *kSettingsSyncBusyFeedback = "请等待同步完成";
constexpr const char *kSettingsOfflineEnabledFeedback = "离线模式已开启";
constexpr const char *kSettingsOfflineDisabledFeedback = "离线模式已关闭";
constexpr const char *kManualWeatherCityEditFeedback = "请进入配网页修改";
constexpr const char *kManualWeatherCityClearConfirmFeedback = "再次确认清除";
constexpr const char *kManualWeatherCityAutoFeedback = "已恢复自动定位";
constexpr const char *kManualNtpSyncFeedback = "正在同步时间...";
constexpr const char *kManualWeatherSyncFeedback = "正在同步天气...";
constexpr const char *kManualSayingSyncFeedback = "正在更新一言...";
constexpr const char *kSoundVolumeFeedbackFormat = "音量 %d%%";
constexpr const char *kSoundIndexFeedbackFormat = "声音 %d";
constexpr const char *kHourlyChimeEnabledFeedback = "整点提醒已开启";
constexpr const char *kHourlyChimeDisabledFeedback = "整点提醒已关闭";
constexpr const char *kAllDayChimeEnabledFeedback = "全天提醒已开启";
constexpr const char *kAllDayChimeDisabledFeedback = "全天提醒已关闭";
constexpr const char *kPageOrderInstructionFeedback = "BOOT交换并保存";
constexpr const char *kWeatherClockRequiredFeedback = "天气时钟不可关闭";
constexpr const char *kWorkPageFeedbackFormat = "%s%s";
constexpr const char *kWorkPageEnabledSuffix = "已开启";
constexpr const char *kWorkPageDisabledSuffix = "已关闭";
constexpr const char *kOfflineSetupConfirmFeedback = "再次确认进入配网";
constexpr const char *kSetupStartFailedFeedback = "配网启动失败";
constexpr const char *kOfflineSetupInstructionFeedback = "请完成配网后关闭";
constexpr const char *kNetworkDiagSyncFeedback = "正在网络检测...";
constexpr const char *kFactoryResetConfirmFeedback = "再次按 BOOT 确认";
constexpr const char *kFactoryResetFailedFeedback = "恢复失败";
constexpr size_t kSettingsFeedbackTextSize = 32;
constexpr size_t kClockDateTextSize = 48;
constexpr const char *kClockDateFormat = "%04d/%02d/%02d / %s";
#define HOURLY_CHIME_SETTING_LOG_FORMAT "hourly chime %s"
#define ALL_DAY_CHIME_SETTING_LOG_FORMAT "hourly chime all-day %s"
#define CHIME_SETTING_ENABLED_LOG_VALUE "enabled"
#define CHIME_SETTING_DISABLED_LOG_VALUE "disabled"
#define MANUAL_WEATHER_CITY_CLEARED_SYNC_LOG "manual weather city cleared, requesting weather sync"
#define MANUAL_NTP_SYNC_REQUESTED_LOG "manual ntp sync requested"
#define MANUAL_WEATHER_SYNC_REQUESTED_LOG "manual weather sync requested"
#define MANUAL_SAYING_SYNC_REQUESTED_LOG "manual daily saying sync requested"
#define MANUAL_NETWORK_DIAG_REQUESTED_LOG "manual network diagnostics requested"
#define FACTORY_RESET_CONFIRM_REQUESTED_LOG "factory reset confirmation requested"
#define FACTORY_RESET_REQUESTED_LOG "factory reset requested from settings"
#define SYSTEM_INFO_REQUESTED_LOG "system info requested from settings"
#define CLOCK_DATE_LABEL_CREATE_FAILED_LOG "clock date label create failed"
#define CLOCK_ALERT_PILL_CREATE_FAILED_LOG "clock alert pill create failed"
#define CLOCK_ALERT_ICON_CANVAS_CREATE_FAILED_LOG "clock alert icon canvas create failed"
#define CLOCK_ALERT_LABEL_CREATE_FAILED_LOG "clock alert label create failed"
#define CLOCK_STATUS_ICON_CANVAS_CREATE_FAILED_FORMAT "clock status icon canvas create failed x=%d"
#define CLOCK_LABEL_CREATE_FAILED_FORMAT "clock label create failed name=%s"
#define CLOCK_ICON_CANVAS_CREATE_FAILED_FORMAT "clock icon canvas create failed name=%s"
#define CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT "clock trend canvas create failed name=%s"
#define CLOCK_FILL_CANVAS_CREATE_FAILED_FORMAT "clock fill canvas create failed name=%s"
#define SETUP_STATUS_LABEL_CREATE_FAILED_FORMAT "setup status label create failed index=%d"
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
constexpr int kSetupStatusLabelX = 26;
constexpr int kSetupStatusLabelWidth = 348;
constexpr int kSetupStatusLabelHeight = 18;

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
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(width, height);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_STATUS_ICON_CANVAS_CREATE_FAILED_FORMAT, x);
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
        draw_1bit_icon(*canvas, width, height, bytes_per_row, bits, lv_color_black(), lv_color_white());
    }
    lv_obj_add_flag(*canvas, LV_OBJ_FLAG_HIDDEN);
}

void center_clock_label_if_created(lv_obj_t *label, const char *name)
{
    if (!label) {
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, name);
        return;
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
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
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(width, height);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_ICON_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
        draw_1bit_icon(*canvas, width, height, bytes_per_row, bits, lv_color_black(), lv_color_white());
    }
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
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(TREND_ICON_WIDTH, TREND_ICON_HEIGHT);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_TREND_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    configure_clock_canvas(*canvas, x, y, TREND_ICON_WIDTH, TREND_ICON_HEIGHT);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas, *buffer, TREND_ICON_WIDTH, TREND_ICON_HEIGHT, LV_IMG_CF_TRUE_COLOR);
        update_trend_icon(*canvas, trend, nullptr);
    }
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
    if (!*buffer) {
        *buffer = alloc_canvas_buffer(width, height);
    }
    *canvas = lv_canvas_create(screen);
    if (!*canvas) {
        ESP_LOGW(TAG, CLOCK_FILL_CANVAS_CREATE_FAILED_FORMAT, name);
        return;
    }
    configure_clock_canvas(*canvas, x, y, width, height);
    if (*buffer) {
        lv_canvas_set_buffer(*canvas, *buffer, width, height, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(*canvas, lv_color_white(), LV_OPA_COVER);
    }
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
        lv_obj_clear_flag(g_alert_pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(g_alert_pill, kClockAlertPillX, kClockAlertPillY);
        lv_obj_set_size(g_alert_pill, kClockAlertPillWidth, kClockAlertPillHeight);
        lv_obj_set_style_bg_color(g_alert_pill, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_alert_pill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_alert_pill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(g_alert_pill, kClockAlertPillRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(g_alert_pill, 0, LV_PART_MAIN);
        lv_obj_add_flag(g_alert_pill, LV_OBJ_FLAG_HIDDEN);

        if (!g_alert_icon_canvas_buf) {
            g_alert_icon_canvas_buf = alloc_canvas_buffer(WARNING_ICON_WIDTH, WARNING_ICON_HEIGHT);
        }
        g_alert_icon_canvas = lv_canvas_create(g_alert_pill);
        if (g_alert_icon_canvas) {
            lv_obj_clear_flag(g_alert_icon_canvas, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(g_alert_icon_canvas, kClockAlertIconX, kClockAlertIconY);
            lv_obj_set_size(g_alert_icon_canvas, WARNING_ICON_WIDTH, WARNING_ICON_HEIGHT);
            lv_obj_set_style_border_width(g_alert_icon_canvas, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(g_alert_icon_canvas, 0, LV_PART_MAIN);
            if (g_alert_icon_canvas_buf) {
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
            }
        } else {
            ESP_LOGW(TAG, "%s", CLOCK_ALERT_ICON_CANVAS_CREATE_FAILED_LOG);
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

    g_weather_city_label = make_label(screen,
                                      kClockWeatherCityLabelX,
                                      kClockWeatherCityLabelY,
                                      kClockWeatherCityLabelWidth,
                                      kClockWeatherCityLabelHeight,
                                      kClockWeatherCityPlaceholder);
    remember_lower_panel_object(g_weather_city_label);
    center_clock_label_if_created(g_weather_city_label, "weather_city");
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
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, "weather_icon");
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
        ESP_LOGW(TAG, CLOCK_LABEL_CREATE_FAILED_FORMAT, "weather_info");
    }
    g_weather_temp_label = make_label(screen,
                                      kClockWeatherMetricLabelX,
                                      kClockWeatherTempLabelY,
                                      kClockWeatherMetricLabelWidth,
                                      kClockWeatherMetricLabelHeight,
                                      kClockWeatherTempPlaceholder);
    g_weather_humi_label = make_label(screen,
                                      kClockWeatherMetricLabelX,
                                      kClockWeatherHumiLabelY,
                                      kClockWeatherMetricLabelWidth,
                                      kClockWeatherMetricLabelHeight,
                                      kClockWeatherHumidityPlaceholder);
    remember_lower_panel_object(g_weather_temp_label);
    remember_lower_panel_object(g_weather_humi_label);
    center_clock_label_if_created(g_weather_temp_label, "weather_temp");
    center_clock_label_if_created(g_weather_humi_label, "weather_humi");

    build_clock_lower_icon(screen,
                           &g_temp_icon_canvas,
                           &g_temp_icon_canvas_buf,
                           kClockTempIconX,
                           kClockTempIconY,
                           TEMP_ICON_WIDTH,
                           TEMP_ICON_HEIGHT,
                           TEMP_ICON_BYTES_PER_ROW,
                           temp_icon_bits,
                           "temp_icon");
    build_clock_lower_icon(screen,
                           &g_humi_icon_canvas,
                           &g_humi_icon_canvas_buf,
                           kClockHumiIconX,
                           kClockHumiIconY,
                           HUMI_ICON_WIDTH,
                           HUMI_ICON_HEIGHT,
                           HUMI_ICON_BYTES_PER_ROW,
                           humi_icon_bits,
                           "humi_icon");
    g_temp_label = make_label(screen,
                              kClockLocalMetricLabelX,
                              kClockLocalTempLabelY,
                              kClockLocalMetricLabelWidth,
                              kClockLocalMetricLabelHeight,
                              "--.-℃");
    g_humi_label = make_label(screen,
                              kClockLocalMetricLabelX,
                              kClockLocalHumiLabelY,
                              kClockLocalMetricLabelWidth,
                              kClockLocalMetricLabelHeight,
                              "--.-%");
    remember_lower_panel_object(g_temp_icon_canvas);
    remember_lower_panel_object(g_humi_icon_canvas);
    remember_lower_panel_object(g_temp_label);
    remember_lower_panel_object(g_humi_label);
    center_clock_label_if_created(g_temp_label, "temp_value");
    center_clock_label_if_created(g_humi_label, "humi_value");
    build_clock_trend_canvas(screen,
                             &g_temp_trend_canvas,
                             &g_temp_trend_canvas_buf,
                             kClockTrendCanvasX,
                             kClockTempTrendCanvasY,
                             g_temp_trend,
                             "temp_trend");
    build_clock_trend_canvas(screen,
                             &g_humi_trend_canvas,
                             &g_humi_trend_canvas_buf,
                             kClockTrendCanvasX,
                             kClockHumiTrendCanvasY,
                             g_humi_trend,
                             "humi_trend");
    remember_lower_panel_object(g_temp_trend_canvas);
    remember_lower_panel_object(g_humi_trend_canvas);
    build_clock_fill_canvas(screen,
                            &g_time_canvas,
                            &g_time_canvas_buf,
                            kClockTimeCanvasX,
                            kClockTimeCanvasY,
                            kClockTimeCanvasWidth,
                            kClockTimeCanvasHeight,
                            "time");

    build_clock_fill_canvas(screen,
                            &g_second_canvas,
                            &g_second_canvas_buf,
                            kClockSecondCanvasX,
                            kClockSecondCanvasY,
                            kClockSecondCanvasWidth,
                            kClockSecondCanvasHeight,
                            "second");

    build_clock_fill_canvas(screen,
                            &g_status_gif_canvas,
                            &g_status_gif_canvas_buf,
                            kClockStatusGifCanvasX,
                            kClockStatusGifCanvasY,
                            STATUS_GIF_WIDTH,
                            STATUS_GIF_HEIGHT,
                            "status_gif");
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
    remember_lower_panel_object(g_panel_sep_a);
    remember_lower_panel_object(g_panel_sep_b);
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
                           "low_battery_icon");
    if (g_low_battery_icon_canvas) {
        lv_obj_add_flag(g_low_battery_icon_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    static const int kSetupStatusLabelY[] = {194, 212, 230, 248, 266, 284};
    static const char *kSetupStatusText[] = {
        "Setup Mode",
        "AP SSID: --",
        "AP Password: --",
        "Portal IP: --",
        "STA SSID: --",
        "STA IP: --",
    };
    constexpr size_t kSetupStatusLabelCount = array_count(kSetupStatusLabelY);
    static_assert(kSetupStatusLabelCount == array_count(kSetupStatusText),
                  "setup status coordinates and text must stay in sync");
    static_assert(kSetupStatusLabelCount == array_count(g_setup_status_labels),
                  "setup status label storage must match the rendered row count");
    for (size_t i = 0; i < kSetupStatusLabelCount; ++i) {
        g_setup_status_labels[i] = make_label_with_font(screen,
                                                        kSetupStatusLabelX,
                                                        kSetupStatusLabelY[i],
                                                        kSetupStatusLabelWidth,
                                                        kSetupStatusLabelHeight,
                                                        kSetupStatusText[i],
                                                        &lv_font_montserrat_14);
        if (g_setup_status_labels[i]) {
            lv_obj_add_flag(g_setup_status_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW(TAG, SETUP_STATUS_LABEL_CREATE_FAILED_FORMAT, static_cast<int>(i));
        }
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
        char date[kClockDateTextSize];
        snprintf(date, sizeof(date), kClockDateFormat,
                 local.tm_year + kTmYearOffset,
                 local.tm_mon + kTmMonthOffset,
                 local.tm_mday,
                 kWeekdayNames[local.tm_wday]);
        if (date_page == kWorkPageWeatherClock) {
            changed |= set_label_text_if_changed(g_date_label, date);
        } else if (date_page == kWorkPageHistory) {
            changed |= set_label_text_if_changed(g_history_date_label, date);
        } else if (date_page == kWorkPageGallery) {
            changed |= set_label_text_if_changed(g_gallery_date_label, date);
        } else if (date_page == kWorkPageCalendar) {
            changed |= set_label_text_if_changed(g_calendar_date_label, date);
        } else if (date_page == kWorkPageWeatherBoard) {
            changed |= set_label_text_if_changed(g_weather_board_date_label, date);
        } else if (date_page == kWorkPageFlipClock) {
            changed |= set_label_text_if_changed(g_flip_clock_date_label, date);
        }
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

void handle_settings_action()
{
    int primary = g_settings_primary_selection;
    if (primary < 0 || primary >= kSettingsPrimaryCount) {
        primary = kSettingsPrimaryNetwork;
    }
    int selected = g_settings_selection;
    int secondary_count = settings_secondary_count(primary);
    if (selected < 0 || selected >= secondary_count) {
        selected = 0;
    }
    g_settings_primary_selection = primary;
    g_settings_selection = selected;
    g_settings_last_activity_tick = xTaskGetTickCount();
    if (g_settings_page_order_mode) {
        normalize_work_page_order();
        int current = g_settings_page_order_selection;
        if (current < 0 || current >= kWorkPageCount) {
            current = 0;
        }
        int next = (current + 1) % kWorkPageCount;
        uint8_t tmp = g_work_page_order[current];
        g_work_page_order[current] = g_work_page_order[next];
        g_work_page_order[next] = tmp;
        g_settings_page_order_selection = next;
        if (save_work_page_order()) {
            g_active_work_page = first_enabled_work_page();
            set_settings_feedback(kSettingsOrderSavedFeedback, kSettingsFeedbackSavedMs);
        } else {
            set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
        }
        return;
    }
    if (!g_settings_focus_secondary) {
        g_settings_focus_secondary = true;
        g_settings_selection = 0;
        reset_settings_confirmation();
        g_settings_feedback[0] = '\0';
        return;
    }
    if (is_settings_sync_busy()) {
        set_settings_feedback(kSettingsSyncBusyFeedback, kSettingsFeedbackBusyMs);
        return;
    }
    if (!(primary == kSettingsPrimarySystem && selected == kSystemSettingsFactoryResetItem)) {
        g_factory_reset_confirm_pending = false;
    }
    if (!(primary == kSettingsPrimarySystem && selected == kSystemSettingsOfflineItem)) {
        g_offline_disable_confirm_pending = false;
    }
    if (!(primary == kSettingsPrimaryNetwork && selected == kNetworkSettingsWeatherCityItem)) {
        g_weather_city_clear_confirm_pending = false;
    }
    if (primary == kSettingsPrimaryNetwork) {
        if (selected == kNetworkSettingsWeatherCityItem) {
            if (!g_has_manual_weather_city) {
                set_settings_feedback(kManualWeatherCityEditFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!g_weather_city_clear_confirm_pending) {
                g_weather_city_clear_confirm_pending = true;
                set_settings_feedback(kManualWeatherCityClearConfirmFeedback, kSettingsTimeoutMs);
                return;
            }
            if (!clear_manual_weather_city()) {
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_weather_city_clear_confirm_pending = false;
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kManualWeatherCityAutoFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            begin_settings_sync(kSettingsSyncWeather, kManualWeatherSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_WEATHER_CITY_CLEARED_SYNC_LOG);
            xEventGroupSetBits(g_app_events, kManualWeatherSyncBit);
            return;
        }
        if (g_offline_mode_ui_enabled) {
            set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
            return;
        }
        if (selected == kNetworkSettingsNtpItem) {
            begin_settings_sync(kSettingsSyncNtp, kManualNtpSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_NTP_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualNtpSyncBit);
        } else if (selected == kNetworkSettingsWeatherItem) {
            begin_settings_sync(kSettingsSyncWeather, kManualWeatherSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_WEATHER_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualWeatherSyncBit);
        } else if (selected == kNetworkSettingsSayingItem) {
            begin_settings_sync(kSettingsSyncSaying, kManualSayingSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_SAYING_SYNC_REQUESTED_LOG);
            xEventGroupSetBits(g_app_events, kManualSayingSyncBit);
        }
        return;
    }
    if (primary == kSettingsPrimarySound) {
        if (selected == kSoundSettingsVolumeItem) {
            int previous = g_chime_volume_percent;
            int next = kDefaultChimeVolumePercent;
            for (int i = 0; i < kChimeVolumeLevelCount; ++i) {
                if (g_chime_volume_percent == kChimeVolumeLevels[i]) {
                    next = kChimeVolumeLevels[(i + 1) % kChimeVolumeLevelCount];
                    break;
                }
            }
            g_chime_volume_percent = next;
            if (!save_hourly_chime_setting()) {
                g_chime_volume_percent = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            char feedback[kSettingsFeedbackTextSize];
            snprintf(feedback, sizeof(feedback), kSoundVolumeFeedbackFormat, g_chime_volume_percent);
            set_settings_feedback(feedback, kSettingsFeedbackDefaultMs);
            request_settings_confirmation_chime();
        } else if (selected == kSoundSettingsSoundItem) {
            int previous = g_chime_sound_index;
            g_chime_sound_index = (g_chime_sound_index + 1) % kChimeSoundCount;
            if (!save_hourly_chime_setting()) {
                g_chime_sound_index = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            char feedback[kSettingsFeedbackTextSize];
            snprintf(feedback, sizeof(feedback), kSoundIndexFeedbackFormat, g_chime_sound_index + 1);
            set_settings_feedback(feedback, kSettingsFeedbackDefaultMs);
            request_settings_confirmation_chime();
        } else if (selected == kSoundSettingsHourlyItem) {
            bool previous = g_hourly_chime_enabled;
            g_hourly_chime_enabled = !g_hourly_chime_enabled;
            if (!save_hourly_chime_setting()) {
                g_hourly_chime_enabled = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_settings_feedback(g_hourly_chime_enabled ? kHourlyChimeEnabledFeedback : kHourlyChimeDisabledFeedback, kSettingsFeedbackDefaultMs);
            ESP_LOGI(TAG,
                     HOURLY_CHIME_SETTING_LOG_FORMAT,
                     g_hourly_chime_enabled ? CHIME_SETTING_ENABLED_LOG_VALUE : CHIME_SETTING_DISABLED_LOG_VALUE);
            if (g_hourly_chime_enabled) {
                request_settings_confirmation_chime();
            }
        } else if (selected == kSoundSettingsAllDayItem) {
            bool previous = g_hourly_chime_all_day;
            g_hourly_chime_all_day = !g_hourly_chime_all_day;
            if (!save_hourly_chime_setting()) {
                g_hourly_chime_all_day = previous;
                set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            set_settings_feedback(g_hourly_chime_all_day ? kAllDayChimeEnabledFeedback : kAllDayChimeDisabledFeedback, kSettingsFeedbackDefaultMs);
            ESP_LOGI(TAG,
                     ALL_DAY_CHIME_SETTING_LOG_FORMAT,
                     g_hourly_chime_all_day ? CHIME_SETTING_ENABLED_LOG_VALUE : CHIME_SETTING_DISABLED_LOG_VALUE);
            if (g_hourly_chime_all_day) {
                request_settings_confirmation_chime();
            }
        }
        return;
    }
    if (primary == kSettingsPrimaryDisplay) {
        if (selected == kDisplaySettingsOrderItem) {
            g_settings_page_order_mode = true;
            g_settings_page_order_selection = 0;
            normalize_work_page_order();
            set_settings_feedback(kPageOrderInstructionFeedback, kSettingsFeedbackInstructionMs);
            return;
        }
        int page = display_settings_item_work_page(selected);
        if (page == kWorkPageWeatherClock) {
            g_work_page_enabled_mask |= (1U << kWorkPageWeatherClock);
            set_settings_feedback(kWeatherClockRequiredFeedback, kSettingsFeedbackDefaultMs);
            return;
        }
        uint8_t previous = g_work_page_enabled_mask;
        g_work_page_enabled_mask ^= (1U << page);
        g_work_page_enabled_mask |= (1U << kWorkPageWeatherClock);
        if (!save_work_page_settings()) {
            g_work_page_enabled_mask = previous;
            set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
            return;
        }
        ensure_active_work_page_enabled();
        char feedback[kSettingsFeedbackTextSize];
        snprintf(feedback, sizeof(feedback), kWorkPageFeedbackFormat,
                 work_page_name(page),
                 is_work_page_enabled(page) ? kWorkPageEnabledSuffix : kWorkPageDisabledSuffix);
        set_settings_feedback(feedback, kSettingsFeedbackDefaultMs);
        return;
    }
    if (primary == kSettingsPrimarySystem) {
        if (selected == kSystemSettingsOfflineItem) {
            if (!g_offline_mode_ui_enabled) {
                if (!set_offline_mode_enabled(true)) {
                    set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                    return;
                }
                g_offline_disable_confirm_pending = false;
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (can_leave_offline_mode_without_setup()) {
                if (!set_offline_mode_enabled(false)) {
                    set_settings_feedback(kSettingsSaveFailedFeedback, kSettingsFeedbackDefaultMs);
                    return;
                }
                g_offline_disable_confirm_pending = false;
                set_settings_feedback(kSettingsOfflineDisabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!g_offline_disable_confirm_pending) {
                g_offline_disable_confirm_pending = true;
                set_settings_feedback(kOfflineSetupConfirmFeedback, kSettingsTimeoutMs);
                return;
            }
            if (!start_wifi_radio(true)) {
                set_settings_feedback(kSetupStartFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_offline_disable_confirm_pending = false;
            set_settings_feedback(kOfflineSetupInstructionFeedback, kSettingsFeedbackInstructionMs);
        } else if (selected == kSystemSettingsNetworkDiagItem) {
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            begin_settings_sync(kSettingsSyncNetworkDiag, kNetworkDiagSyncFeedback);
            ESP_LOGI(TAG, "%s", MANUAL_NETWORK_DIAG_REQUESTED_LOG);
            network_diag_reset();
            g_settings_requested = false;
            g_network_diag_page_requested = true;
            g_settings_focus_secondary = true;
            g_settings_primary_selection = kSettingsPrimarySystem;
            g_settings_selection = 0;
            g_info_page_until_tick = 0;
            xEventGroupSetBits(g_app_events, kNetworkDiagBit);
        } else if (selected == kSystemSettingsFactoryResetItem) {
            if (!g_factory_reset_confirm_pending) {
                g_factory_reset_confirm_pending = true;
                set_settings_feedback(kFactoryResetConfirmFeedback, kSettingsTimeoutMs);
                ESP_LOGW(TAG, "%s", FACTORY_RESET_CONFIRM_REQUESTED_LOG);
                return;
            }
            ESP_LOGW(TAG, "%s", FACTORY_RESET_REQUESTED_LOG);
            if (!clear_saved_config()) {
                set_settings_feedback(kFactoryResetFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            if (!start_wifi_radio(true)) {
                set_settings_feedback(kSetupStartFailedFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            g_settings_requested = false;
            g_settings_page_order_mode = false;
            g_factory_reset_confirm_pending = false;
            g_offline_disable_confirm_pending = false;
        } else if (selected == kSystemSettingsInfoItem) {
            g_settings_requested = false;
            g_settings_page_order_mode = false;
            g_factory_reset_confirm_pending = false;
            g_boot_info_requested = true;
            g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kSettingsTimeoutMs);
            ESP_LOGI(TAG, "%s", SYSTEM_INFO_REQUESTED_LOG);
        } else if (selected == kSystemSettingsOtaItem) {
            if (g_offline_mode_ui_enabled) {
                set_settings_feedback(kSettingsOfflineEnabledFeedback, kSettingsFeedbackDefaultMs);
                return;
            }
            ota_handle_info_key();
        }
    }
}
