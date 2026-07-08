// 绘制第五页天气看板，复用 QWeather 缓存并避免秒级刷新。
#include "ui_views.h"

#include "network_services.h"

namespace {

lv_obj_t *s_city_label;
lv_obj_t *s_current_icon_label;
lv_obj_t *s_current_temp_label;
lv_obj_t *s_current_unit_label;
lv_obj_t *s_current_text_label;
lv_obj_t *s_today_range_label;
lv_obj_t *s_air_label;
lv_obj_t *s_humidity_label;
lv_obj_t *s_wind_label;
lv_obj_t *s_sunrise_label;
lv_obj_t *s_sunset_label;
lv_obj_t *s_sun_countdown_label;
lv_obj_t *s_alert_label;
lv_obj_t *s_advice_label;

struct ForecastCardUi {
    lv_obj_t *box = nullptr;
    lv_obj_t *date = nullptr;
    lv_obj_t *icon = nullptr;
    lv_obj_t *text = nullptr;
    lv_obj_t *range = nullptr;
};

ForecastCardUi s_cards[kWeatherForecastDays];
constexpr int kTmYearOffset = 1900;
constexpr int kTmMonthOffset = 1;
constexpr const char *kForecastDateFormat = "%d-%d-%d";
constexpr int kForecastDateFieldCount = 3;
constexpr const char *kWeatherBoardTimeParseFormat = "%d:%d";
constexpr const char *kWeatherBoardDash = "--";
constexpr const char *kWeatherBoardShortDatePlaceholder = "--/--";
constexpr const char *kWeatherBoardUnknownIcon = "999";
constexpr const char *kWeatherBoardWaitingData = "等待数据";
constexpr const char *kWeatherBoardSyncing = "同步中";
constexpr const char *kWeatherBoardTodayRangePlaceholder = "今日 --/--C";
constexpr const char *kWeatherBoardAirPlaceholder = "AQI --";
constexpr const char *kWeatherBoardHumidityPlaceholder = "湿度 --%";
constexpr const char *kWeatherBoardWindPlaceholder = "-- --级";
constexpr const char *kWeatherBoardTimePlaceholder = "--:--";
constexpr const char *kWeatherBoardSunrisePlaceholder = "日出 --:--";
constexpr const char *kWeatherBoardSunsetPlaceholder = "日落 --:--";
constexpr const char *kWeatherBoardSunCountdownPlaceholder = "距日落 --:--";
constexpr const char *kWeatherBoardAlertPlaceholder = "预警 --";
constexpr const char *kWeatherBoardAlertPrefix = "预警 ";
constexpr const char *kWeatherBoardAlertSeparator = " / ";
constexpr const char *kWeatherBoardAdvicePlaceholder = "等待更多天气数据";
constexpr int kWeatherBoardMaxAlertTitles = 3;
constexpr int kWeatherBoardWeekdayCount = 7;
constexpr const char *kWeatherBoardWeekdayNames[kWeatherBoardWeekdayCount] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六",
};
constexpr const char *kForecastShortDateFormat = "%d日";
constexpr const char *kForecastDateLineFormat = "%s\n%s";
constexpr const char *kForecastTempRangeFormat = "%s/%s";
constexpr const char *kWeatherBoardCurrentUnitText = "C";
constexpr const char *kWeatherBoardTodayRangeFormat = "今日 %s/%sC";
constexpr const char *kWeatherBoardAirFormat = "AQI %s %s";
constexpr const char *kWeatherBoardHumidityFormat = "湿度 %s%%";
constexpr const char *kWeatherBoardWindFormat = "%s %s级";
constexpr const char *kWeatherBoardSunriseFormat = "日出 %s";
constexpr const char *kWeatherBoardSunsetFormat = "日落 %s";
constexpr const char *kWeatherBoardSunCountdownFormat = "距%s %02d:%02d";
constexpr const char *kWeatherBoardSunTargetSunrise = "日出";
constexpr const char *kWeatherBoardSunTargetSunset = "日落";
constexpr int kForecastCardX[kWeatherForecastDays] = {138, 180, 222, 264, 306, 348};
template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr int kForecastCardY = 66;
constexpr int kForecastCardW = 34;
constexpr int kForecastCardH = 126;
constexpr int kForecastCardDateH = 30;
constexpr int kForecastCardIconY = 35;
constexpr int kForecastCardIconH = 36;
constexpr int kForecastCardTextY = 72;
constexpr int kForecastCardTextH = 34;
constexpr int kForecastCardRangeY = 108;
constexpr int kForecastCardRangeH = 16;
constexpr int kWeatherBoardTopLineX = 18;
constexpr int kWeatherBoardTopLineY = 54;
constexpr int kWeatherBoardTopLineW = 364;
constexpr int kWeatherBoardTopLineH = 4;
constexpr int kWeatherBoardCurrentCityX = 20;
constexpr int kWeatherBoardCurrentCityY = 66;
constexpr int kWeatherBoardCurrentCityW = 135;
constexpr int kWeatherBoardCurrentCityH = 28;
constexpr int kWeatherBoardCurrentTempX = 20;
constexpr int kWeatherBoardCurrentTempY = 86;
constexpr int kWeatherBoardCurrentTempW = 88;
constexpr int kWeatherBoardCurrentTempH = 54;
constexpr int kWeatherBoardCurrentUnitX = 88;
constexpr int kWeatherBoardCurrentUnitY = 96;
constexpr int kWeatherBoardCurrentUnitW = 24;
constexpr int kWeatherBoardCurrentUnitH = 32;
constexpr int kWeatherBoardCurrentIconX = 20;
constexpr int kWeatherBoardCurrentIconY = 143;
constexpr int kWeatherBoardCurrentIconW = 42;
constexpr int kWeatherBoardCurrentIconH = 40;
constexpr int kWeatherBoardCurrentTextX = 62;
constexpr int kWeatherBoardCurrentTextY = 151;
constexpr int kWeatherBoardCurrentTextW = 92;
constexpr int kWeatherBoardCurrentTextH = 24;
constexpr int kWeatherBoardTodayRangeX = 20;
constexpr int kWeatherBoardTodayRangeY = 179;
constexpr int kWeatherBoardTodayRangeW = 134;
constexpr int kWeatherBoardTodayRangeH = 22;
constexpr int kWeatherBoardDetailLineX = 20;
constexpr int kWeatherBoardDetailLineY = 196;
constexpr int kWeatherBoardDetailLineW = 360;
constexpr int kWeatherBoardDetailLineH = 2;
constexpr int kWeatherBoardDetailTopY = 202;
constexpr int kWeatherBoardDetailBottomY = 224;
constexpr int kWeatherBoardDetailLabelH = 22;
constexpr int kWeatherBoardSunLabelH = 20;
constexpr int kWeatherBoardLeftColumnX = 20;
constexpr int kWeatherBoardMiddleColumnX = 132;
constexpr int kWeatherBoardRightColumnX = 238;
constexpr int kWeatherBoardAirLabelW = 110;
constexpr int kWeatherBoardHumidityLabelW = 86;
constexpr int kWeatherBoardWindLabelW = 142;
constexpr int kWeatherBoardSunriseLabelW = 110;
constexpr int kWeatherBoardSunsetLabelW = 98;
constexpr int kWeatherBoardSunCountdownLabelW = 142;
constexpr int kWeatherBoardAlertX = 20;
constexpr int kWeatherBoardAlertY = 246;
constexpr int kWeatherBoardAlertW = 360;
constexpr int kWeatherBoardAlertH = 22;
constexpr int kWeatherBoardAdviceX = 20;
constexpr int kWeatherBoardAdviceY = 272;
constexpr int kWeatherBoardAdviceW = 360;
constexpr int kWeatherBoardAdviceH = 20;
constexpr size_t kForecastDateLineSize = 24;
constexpr size_t kForecastShortDateSize = 8;
constexpr size_t kForecastTempRangeSize = 20;
constexpr size_t kCurrentTempLineSize = 12;
constexpr size_t kTodayRangeLineSize = 32;
constexpr size_t kWeatherBoardHumidityLineSize = 24;
constexpr size_t kWeatherBoardAirLineSize = 40;
constexpr size_t kWeatherBoardWindLineSize = 48;
constexpr size_t kWeatherBoardSunTimeLineSize = 24;
constexpr size_t kWeatherBoardSunCountdownLineSize = 24;
constexpr size_t kWeatherBoardAlertLineSize = 160;
constexpr int kMinutesPerHour = 60;
constexpr int kSecondsPerMinute = 60;
#define WEATHER_BOARD_FORECAST_CARD_CREATE_FAILED_FORMAT "weather forecast card %d create failed"
static_assert(array_count(kWeatherBoardWeekdayNames) == kWeatherBoardWeekdayCount,
              "weather board weekday names must match weekday count");
static_assert(array_count(kForecastCardX) == kWeatherForecastDays,
              "weather forecast card positions must match forecast day count");
static_assert(kWeatherBoardTopLineW > 0 && kWeatherBoardTopLineH > 0,
              "weather board top line size must be positive");
static_assert(kWeatherBoardCurrentCityW > 0 && kWeatherBoardCurrentCityH > 0,
              "weather board current city label size must be positive");
static_assert(kWeatherBoardCurrentTempW > 0 && kWeatherBoardCurrentTempH > 0,
              "weather board current temperature label size must be positive");
static_assert(kWeatherBoardCurrentUnitW > 0 && kWeatherBoardCurrentUnitH > 0,
              "weather board current unit label size must be positive");
static_assert(kWeatherBoardCurrentIconW > 0 && kWeatherBoardCurrentIconH > 0,
              "weather board current icon label size must be positive");
static_assert(kWeatherBoardCurrentTextW > 0 && kWeatherBoardCurrentTextH > 0,
              "weather board current text label size must be positive");
static_assert(kWeatherBoardTodayRangeW > 0 && kWeatherBoardTodayRangeH > 0,
              "weather board today range label size must be positive");
static_assert(kWeatherBoardDetailLineW > 0 && kWeatherBoardDetailLineH > 0,
              "weather board detail separator size must be positive");

void set_weather_label_align(lv_obj_t *label, lv_text_align_t align)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
}

void set_weather_label_font(lv_obj_t *label, const lv_font_t *font)
{
    if (!label || !font) {
        return;
    }
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
}

void set_weather_label_long_mode(lv_obj_t *label, lv_label_long_mode_t mode)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, mode);
}

void style_weather_card(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

template <typename... Args>
void format_weather_board_text_or_fallback(char *out, size_t out_len, const char *fallback, const char *format, Args... args)
{
    if (!out || out_len == 0) {
        return;
    }
    int written = snprintf(out, out_len, format, args...);
    if (written < 0 || static_cast<size_t>(written) >= out_len) {
        strlcpy(out, fallback, out_len);
    }
}

void set_card_visible(ForecastCardUi &card, bool visible)
{
    set_obj_visible(card.box, visible);
    set_obj_visible(card.date, visible);
    set_obj_visible(card.icon, visible);
    set_obj_visible(card.text, visible);
    set_obj_visible(card.range, visible);
}

const char *text_or_dash(const char *text)
{
    return text && text[0] ? text : kWeatherBoardDash;
}

const char *weather_icon_or_default(const char *icon)
{
    return weather_icon_text(icon && icon[0] ? icon : kWeatherBoardUnknownIcon);
}

const WeatherForecastDay *forecast_day_or_null(const WeatherForecastData &forecast, int index)
{
    if (!forecast.ready || index < 0 || index >= forecast.count || !forecast.days[index].valid) {
        return nullptr;
    }
    return &forecast.days[index];
}

bool parse_weather_board_time(const char *text, int *hour, int *minute)
{
    if (!text || !hour || !minute) {
        return false;
    }
    int parsed_hour = 0;
    int parsed_minute = 0;
    if (sscanf(text, kWeatherBoardTimeParseFormat, &parsed_hour, &parsed_minute) != 2) {
        return false;
    }
    if (parsed_hour < 0 || parsed_hour > 23 || parsed_minute < 0 || parsed_minute > 59) {
        return false;
    }
    *hour = parsed_hour;
    *minute = parsed_minute;
    return true;
}

time_t weather_board_time_on_day(const struct tm &local, const char *hhmm, int day_offset)
{
    int hour = 0;
    int minute = 0;
    if (!parse_weather_board_time(hhmm, &hour, &minute)) {
        return (time_t)-1;
    }
    struct tm target = local;
    target.tm_sec = 0;
    target.tm_min = minute;
    target.tm_hour = hour;
    target.tm_mday += day_offset;
    target.tm_isdst = -1;
    return mktime(&target);
}

void set_sun_countdown_placeholder(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    strlcpy(out, kWeatherBoardSunCountdownPlaceholder, out_len);
}

void format_weather_board_sun_countdown(const struct tm &local,
                                        const WeatherForecastData &forecast,
                                        char *out,
                                        size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    const WeatherForecastDay *today = forecast_day_or_null(forecast, 0);
    if (!today || !today->sunrise[0] || !today->sunset[0]) {
        set_sun_countdown_placeholder(out, out_len);
        return;
    }
    struct tm now_tm = local;
    time_t now = mktime(&now_tm);
    time_t sunrise = weather_board_time_on_day(local, today->sunrise, 0);
    time_t sunset = weather_board_time_on_day(local, today->sunset, 0);
    if (now <= 0 || sunrise <= 0 || sunset <= 0) {
        set_sun_countdown_placeholder(out, out_len);
        return;
    }
    const char *target_name = kWeatherBoardSunTargetSunset;
    time_t target = sunset;
    if (now < sunrise) {
        target_name = kWeatherBoardSunTargetSunrise;
        target = sunrise;
    } else if (now >= sunset) {
        target_name = kWeatherBoardSunTargetSunrise;
        const WeatherForecastDay *tomorrow = forecast_day_or_null(forecast, 1);
        target = weather_board_time_on_day(local,
                                           tomorrow && tomorrow->sunrise[0] ? tomorrow->sunrise : today->sunrise,
                                           1);
    }
    if (target <= now) {
        set_sun_countdown_placeholder(out, out_len);
        return;
    }
    int total_minutes = (int)((target - now + (kSecondsPerMinute - 1)) / kSecondsPerMinute);
    int hours = total_minutes / kMinutesPerHour;
    int minutes = total_minutes % kMinutesPerHour;
    format_weather_board_text_or_fallback(out,
                                          out_len,
                                          kWeatherBoardSunCountdownPlaceholder,
                                          kWeatherBoardSunCountdownFormat,
                                          target_name,
                                          hours,
                                          minutes);
}

bool parse_forecast_date(const char *date, int &year, int &month, int &day)
{
    if (!date) {
        return false;
    }
    return sscanf(date, kForecastDateFormat, &year, &month, &day) == kForecastDateFieldCount;
}

const char *weekday_name_from_date(const char *date)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_forecast_date(date, year, month, day)) {
        return kWeatherBoardDash;
    }
    struct tm tm_value = {};
    tm_value.tm_year = year - kTmYearOffset;
    tm_value.tm_mon = month - kTmMonthOffset;
    tm_value.tm_mday = day;
    tm_value.tm_isdst = -1;
    time_t epoch = mktime(&tm_value);
    if (epoch <= 0) {
        return kWeatherBoardDash;
    }
    localtime_r(&epoch, &tm_value);
    if (tm_value.tm_wday < 0 || tm_value.tm_wday >= kWeatherBoardWeekdayCount) {
        return kWeatherBoardDash;
    }
    return kWeatherBoardWeekdayNames[tm_value.tm_wday];
}

void format_short_date(const char *date, char *out, size_t out_len)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!out || out_len == 0) {
        return;
    }
    if (!parse_forecast_date(date, year, month, day)) {
        strlcpy(out, kWeatherBoardShortDatePlaceholder, out_len);
        return;
    }
    format_weather_board_text_or_fallback(out,
                                          out_len,
                                          kWeatherBoardShortDatePlaceholder,
                                          kForecastShortDateFormat,
                                          day);
}

void format_today_range(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    format_weather_board_text_or_fallback(out,
                                          out_len,
                                          kWeatherBoardTodayRangePlaceholder,
                                          kWeatherBoardTodayRangeFormat,
                                          text_or_dash(day.temp_min),
                                          text_or_dash(day.temp_max));
}

void format_forecast_date_line(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    char date_short[kForecastShortDateSize] = {};
    format_short_date(day.date, date_short, sizeof(date_short));
    format_weather_board_text_or_fallback(out,
                                          out_len,
                                          kWeatherBoardShortDatePlaceholder,
                                          kForecastDateLineFormat,
                                          weekday_name_from_date(day.date),
                                          date_short);
}

void format_forecast_temp_range(const WeatherForecastDay &day, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    format_weather_board_text_or_fallback(out,
                                          out_len,
                                          kWeatherBoardShortDatePlaceholder,
                                          kForecastTempRangeFormat,
                                          text_or_dash(day.temp_min),
                                          text_or_dash(day.temp_max));
}

void format_weather_board_alert_line(const WeatherAlertData &alert, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!alert.active || alert.count <= 0 || !alert.titles[0][0]) {
        strlcpy(out, kWeatherBoardAlertPlaceholder, out_len);
        return;
    }
    strlcpy(out, kWeatherBoardAlertPrefix, out_len);
    for (int i = 0; i < alert.count && i < kWeatherBoardMaxAlertTitles; ++i) {
        if (i > 0) {
            strlcat(out, kWeatherBoardAlertSeparator, out_len);
        }
        strlcat(out, alert.titles[i], out_len);
    }
}

bool update_forecast_card(ForecastCardUi &card, const WeatherForecastDay *day)
{
    bool changed = false;
    if (!day || !day->valid) {
        changed |= set_label_text_if_changed(card.date, kWeatherBoardDash);
        changed |= set_label_text_if_changed(card.icon, weather_icon_text(kWeatherBoardUnknownIcon));
        changed |= set_label_text_if_changed(card.text, kWeatherBoardDash);
        changed |= set_label_text_if_changed(card.range, kWeatherBoardShortDatePlaceholder);
        return changed;
    }
    char date_line[kForecastDateLineSize] = {};
    char temp_range[kForecastTempRangeSize] = {};
    format_forecast_date_line(*day, date_line, sizeof(date_line));
    format_forecast_temp_range(*day, temp_range, sizeof(temp_range));
    changed |= set_label_text_if_changed(card.date, date_line);
    changed |= set_label_text_if_changed(card.icon, weather_icon_or_default(day->icon));
    changed |= set_label_text_if_changed(card.text, text_or_dash(day->text));
    changed |= set_label_text_if_changed(card.range, temp_range);
    return changed;
}

} // namespace

void build_weather_board_page()
{
    if (g_weather_board_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_weather_board_root = screen;
    lv_obj_add_flag(g_weather_board_root, LV_OBJ_FLAG_HIDDEN);

    build_battery_icon(screen, g_weather_board_battery_segments);
    build_work_page_status_bar(screen,
                               kWorkPageWeatherBoard,
                               &g_weather_board_date_label,
                               &g_weather_board_summary_label,
                               &g_weather_board_status_time_label,
                               true);

    lv_obj_t *top_line = make_bar(screen,
                                  kWeatherBoardTopLineX,
                                  kWeatherBoardTopLineY,
                                  kWeatherBoardTopLineW,
                                  kWeatherBoardTopLineH);
    set_obj_black(top_line, true);

    s_city_label = make_label(screen,
                              kWeatherBoardCurrentCityX,
                              kWeatherBoardCurrentCityY,
                              kWeatherBoardCurrentCityW,
                              kWeatherBoardCurrentCityH,
                              kWeatherBoardWaitingData);
    set_weather_label_align(s_city_label, LV_TEXT_ALIGN_LEFT);

    s_current_temp_label = make_label_with_font(screen,
                                                kWeatherBoardCurrentTempX,
                                                kWeatherBoardCurrentTempY,
                                                kWeatherBoardCurrentTempW,
                                                kWeatherBoardCurrentTempH,
                                                kWeatherBoardDash,
                                                &lv_font_montserrat_48);
    set_weather_label_align(s_current_temp_label, LV_TEXT_ALIGN_LEFT);
    s_current_unit_label = make_label_with_font(screen,
                                                kWeatherBoardCurrentUnitX,
                                                kWeatherBoardCurrentUnitY,
                                                kWeatherBoardCurrentUnitW,
                                                kWeatherBoardCurrentUnitH,
                                                kWeatherBoardCurrentUnitText,
                                                &lv_font_montserrat_24);
    set_weather_label_align(s_current_unit_label, LV_TEXT_ALIGN_LEFT);

    s_current_icon_label = make_label(screen,
                                      kWeatherBoardCurrentIconX,
                                      kWeatherBoardCurrentIconY,
                                      kWeatherBoardCurrentIconW,
                                      kWeatherBoardCurrentIconH,
                                      weather_icon_text(kWeatherBoardUnknownIcon));
    set_weather_label_font(s_current_icon_label, &qweather_icons_36);
    set_weather_label_align(s_current_icon_label, LV_TEXT_ALIGN_CENTER);
    s_current_text_label = make_label(screen,
                                      kWeatherBoardCurrentTextX,
                                      kWeatherBoardCurrentTextY,
                                      kWeatherBoardCurrentTextW,
                                      kWeatherBoardCurrentTextH,
                                      kWeatherBoardDash);
    s_today_range_label = make_label(screen,
                                     kWeatherBoardTodayRangeX,
                                     kWeatherBoardTodayRangeY,
                                     kWeatherBoardTodayRangeW,
                                     kWeatherBoardTodayRangeH,
                                     kWeatherBoardTodayRangePlaceholder);

    for (int i = 0; i < kWeatherForecastDays; ++i) {
        int x = kForecastCardX[i];
        int y = kForecastCardY;
        s_cards[i].box = lv_obj_create(screen);
        if (!s_cards[i].box) {
            ESP_LOGW(TAG, WEATHER_BOARD_FORECAST_CARD_CREATE_FAILED_FORMAT, i);
        } else {
            lv_obj_set_pos(s_cards[i].box, x, y);
            lv_obj_set_size(s_cards[i].box, kForecastCardW, kForecastCardH);
            style_weather_card(s_cards[i].box);
        }

        s_cards[i].date = make_label(screen, x, y, kForecastCardW, kForecastCardDateH, kWeatherBoardDash);
        set_weather_label_long_mode(s_cards[i].date, LV_LABEL_LONG_WRAP);
        set_weather_label_align(s_cards[i].date, LV_TEXT_ALIGN_CENTER);
        s_cards[i].icon = make_label(screen,
                                     x,
                                     y + kForecastCardIconY,
                                     kForecastCardW,
                                     kForecastCardIconH,
                                     weather_icon_text(kWeatherBoardUnknownIcon));
        set_weather_label_font(s_cards[i].icon, &qweather_icons_36);
        set_weather_label_align(s_cards[i].icon, LV_TEXT_ALIGN_CENTER);
        s_cards[i].text = make_label(screen,
                                     x,
                                     y + kForecastCardTextY,
                                     kForecastCardW,
                                     kForecastCardTextH,
                                     kWeatherBoardDash);
        set_weather_label_long_mode(s_cards[i].text, LV_LABEL_LONG_WRAP);
        set_weather_label_align(s_cards[i].text, LV_TEXT_ALIGN_CENTER);
        s_cards[i].range = make_label(screen,
                                      x,
                                      y + kForecastCardRangeY,
                                      kForecastCardW,
                                      kForecastCardRangeH,
                                      kWeatherBoardShortDatePlaceholder);
        set_weather_label_align(s_cards[i].range, LV_TEXT_ALIGN_CENTER);
        set_weather_label_font(s_cards[i].range, &lv_font_montserrat_12);
    }

    lv_obj_t *detail_line = make_bar(screen,
                                     kWeatherBoardDetailLineX,
                                     kWeatherBoardDetailLineY,
                                     kWeatherBoardDetailLineW,
                                     kWeatherBoardDetailLineH);
    set_obj_black(detail_line, true);
    s_air_label = make_label(screen,
                             kWeatherBoardLeftColumnX,
                             kWeatherBoardDetailTopY,
                             kWeatherBoardAirLabelW,
                             kWeatherBoardDetailLabelH,
                             kWeatherBoardAirPlaceholder);
    s_humidity_label = make_label(screen,
                                  kWeatherBoardMiddleColumnX,
                                  kWeatherBoardDetailTopY,
                                  kWeatherBoardHumidityLabelW,
                                  kWeatherBoardDetailLabelH,
                                  kWeatherBoardHumidityPlaceholder);
    s_wind_label = make_label(screen,
                              kWeatherBoardRightColumnX,
                              kWeatherBoardDetailTopY,
                              kWeatherBoardWindLabelW,
                              kWeatherBoardDetailLabelH,
                              kWeatherBoardWindPlaceholder);
    s_sunrise_label = make_label(screen,
                                 kWeatherBoardLeftColumnX,
                                 kWeatherBoardDetailBottomY,
                                 kWeatherBoardSunriseLabelW,
                                 kWeatherBoardSunLabelH,
                                 kWeatherBoardSunrisePlaceholder);
    s_sunset_label = make_label(screen,
                                kWeatherBoardMiddleColumnX,
                                kWeatherBoardDetailBottomY,
                                kWeatherBoardSunsetLabelW,
                                kWeatherBoardSunLabelH,
                                kWeatherBoardSunsetPlaceholder);
    s_sun_countdown_label = make_label(screen,
                                       kWeatherBoardRightColumnX,
                                       kWeatherBoardDetailBottomY,
                                       kWeatherBoardSunCountdownLabelW,
                                       kWeatherBoardSunLabelH,
                                       kWeatherBoardSunCountdownPlaceholder);
    s_alert_label = make_label(screen,
                               kWeatherBoardAlertX,
                               kWeatherBoardAlertY,
                               kWeatherBoardAlertW,
                               kWeatherBoardAlertH,
                               kWeatherBoardAlertPlaceholder);
    s_advice_label = make_label(screen,
                                kWeatherBoardAdviceX,
                                kWeatherBoardAdviceY,
                                kWeatherBoardAdviceW,
                                kWeatherBoardAdviceH,
                                kWeatherBoardAdvicePlaceholder);
    set_weather_label_long_mode(s_advice_label, LV_LABEL_LONG_WRAP);
    set_weather_label_align(s_advice_label, LV_TEXT_ALIGN_LEFT);
}

bool update_weather_board_page(const struct tm &local)
{
    build_weather_board_page();
    if (!g_weather_board_root) {
        return false;
    }

    WeatherData weather = {};
    WeatherAlertData alert = {};
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    get_weather_snapshot(&weather, &alert);
    get_weather_forecast_snapshot(&forecast);
    get_weather_air_snapshot(&air);
    EventBits_t bits = xEventGroupGetBits(g_app_events);
    bool weather_ready = (bits & kWeatherReadyBit) != 0;
    bool changed = update_work_page_status_time(g_weather_board_status_time_label, local);
    changed |= update_work_page_sensor_summary(g_weather_board_summary_label);

    if (weather_ready) {
        char temp_line[kCurrentTempLineSize] = {};
        char today_range[kTodayRangeLineSize] = {};
        strlcpy(temp_line, text_or_dash(weather.temp), sizeof(temp_line));
        changed |= set_label_text_if_changed(s_city_label, text_or_dash(weather.city));
        changed |= set_label_text_if_changed(s_current_temp_label, temp_line);
        changed |= set_label_text_if_changed(s_current_icon_label, weather_icon_or_default(weather.icon));
        changed |= set_label_text_if_changed(s_current_text_label, text_or_dash(weather.text));
        if (forecast.ready && forecast.count > 0 && forecast.days[0].valid) {
            format_today_range(forecast.days[0], today_range, sizeof(today_range));
        } else {
            strlcpy(today_range, kWeatherBoardTodayRangePlaceholder, sizeof(today_range));
        }
        changed |= set_label_text_if_changed(s_today_range_label, today_range);
    } else {
        changed |= set_label_text_if_changed(s_city_label, kWeatherBoardDash);
        changed |= set_label_text_if_changed(s_current_temp_label, kWeatherBoardDash);
        changed |= set_label_text_if_changed(s_current_icon_label, weather_icon_text(kWeatherBoardUnknownIcon));
        changed |= set_label_text_if_changed(s_current_text_label,
                                             (bits & kWifiConnectedBit) ? kWeatherBoardSyncing : kWeatherBoardWaitingData);
        changed |= set_label_text_if_changed(s_today_range_label, kWeatherBoardTodayRangePlaceholder);
    }

    for (int i = 0; i < kWeatherForecastDays; ++i) {
        const WeatherForecastDay *day = forecast_day_or_null(forecast, i);
        set_card_visible(s_cards[i], true);
        changed |= update_forecast_card(s_cards[i], day);
    }

    char humi_line[kWeatherBoardHumidityLineSize] = {};
    char air_line[kWeatherBoardAirLineSize] = {};
    char wind_line[kWeatherBoardWindLineSize] = {};
    char sunrise_line[kWeatherBoardSunTimeLineSize] = {};
    char sunset_line[kWeatherBoardSunTimeLineSize] = {};
    char sun_countdown_line[kWeatherBoardSunCountdownLineSize] = {};
    char alert_line[kWeatherBoardAlertLineSize] = {};
    const WeatherForecastDay *today = forecast_day_or_null(forecast, 0);
    if (air.ready) {
        format_weather_board_text_or_fallback(air_line,
                                              sizeof(air_line),
                                              kWeatherBoardAirPlaceholder,
                                              kWeatherBoardAirFormat,
                                              text_or_dash(air.aqi),
                                              text_or_dash(air.category));
    } else {
        strlcpy(air_line, kWeatherBoardAirPlaceholder, sizeof(air_line));
    }
    format_weather_board_text_or_fallback(humi_line,
                                          sizeof(humi_line),
                                          kWeatherBoardHumidityPlaceholder,
                                          kWeatherBoardHumidityFormat,
                                          today && today->humidity[0] ? today->humidity : text_or_dash(weather.humidity));
    format_weather_board_text_or_fallback(wind_line,
                                          sizeof(wind_line),
                                          kWeatherBoardWindPlaceholder,
                                          kWeatherBoardWindFormat,
                                          today ? text_or_dash(today->wind_dir) : kWeatherBoardDash,
                                          today ? text_or_dash(today->wind_scale) : kWeatherBoardDash);
    format_weather_board_text_or_fallback(sunrise_line,
                                          sizeof(sunrise_line),
                                          kWeatherBoardSunrisePlaceholder,
                                          kWeatherBoardSunriseFormat,
                                          today && today->sunrise[0] ? today->sunrise : kWeatherBoardTimePlaceholder);
    format_weather_board_text_or_fallback(sunset_line,
                                          sizeof(sunset_line),
                                          kWeatherBoardSunsetPlaceholder,
                                          kWeatherBoardSunsetFormat,
                                          today && today->sunset[0] ? today->sunset : kWeatherBoardTimePlaceholder);
    format_weather_board_sun_countdown(local, forecast, sun_countdown_line, sizeof(sun_countdown_line));
    format_weather_board_alert_line(alert, alert_line, sizeof(alert_line));
    changed |= set_label_text_if_changed(s_air_label, air_line);
    changed |= set_label_text_if_changed(s_humidity_label, humi_line);
    changed |= set_label_text_if_changed(s_wind_label, wind_line);
    changed |= set_label_text_if_changed(s_sunrise_label, sunrise_line);
    changed |= set_label_text_if_changed(s_sunset_label, sunset_line);
    changed |= set_label_text_if_changed(s_sun_countdown_label, sun_countdown_line);
    changed |= set_label_text_if_changed(s_alert_label, alert_line);
    changed |= set_label_text_if_changed(s_advice_label,
                                         forecast.ready && forecast.advice[0] ? forecast.advice : kWeatherBoardAdvicePlaceholder);
    return changed;
}
