// 对接 IP 定位、QWeather 城市查询、实时天气和天气预警接口。
#include "network_services.h"
#include "qweather_forecast_parser.h"
#include "qweather_location_text.h"
#include "qweather_response.h"

#include <stdarg.h>

namespace {
constexpr const char *kQweatherApiHost = "devapi.qweather.com";
constexpr const char *kQweatherGeoApiHost = "geoapi.qweather.com";
}

const char *qweather_api_host()
{
    return kQweatherApiHost;
}

namespace {
constexpr size_t kQweatherCityResponseBufferSize = 8192;
constexpr size_t kQweatherNowResponseBufferSize = 8192;
constexpr size_t kQweatherAlertResponseBufferSize = 16384;
constexpr size_t kQweatherDailyResponseBufferSize = 24576;
constexpr size_t kQweatherAirResponseBufferSize = 8192;
constexpr size_t kQweatherEncodedLocationSize = 128;
constexpr size_t kQweatherApiUrlSize = 512;
constexpr size_t kQweatherAlertUrlSize = 256;
constexpr size_t kWeatherAlertEventNameSize = 24;
constexpr size_t kWeatherAlertColorCodeSize = 16;
constexpr size_t kWeatherAlertHeadlineSize = 64;
constexpr size_t kQweatherCityIdSize = 24;
constexpr size_t kWeatherCityNameSize = 32;
static_assert(kQweatherCityResponseBufferSize > 1, "QWeather city response buffer must fit text and NUL");
static_assert(kQweatherNowResponseBufferSize > 1, "QWeather now response buffer must fit text and NUL");
static_assert(kQweatherAlertResponseBufferSize > kQweatherNowResponseBufferSize,
              "QWeather alert response buffer should remain larger than now response buffer");
static_assert(kQweatherDailyResponseBufferSize > kQweatherAlertResponseBufferSize,
              "QWeather daily response buffer should remain the largest weather response buffer");
static_assert(kQweatherAirResponseBufferSize > 1, "QWeather air response buffer must fit text and NUL");
static_assert(kQweatherEncodedLocationSize > kManualWeatherCityLen,
              "encoded weather location buffer must fit manual city text");
static_assert(kQweatherApiUrlSize > kQweatherEncodedLocationSize,
              "general QWeather API URL buffer must fit encoded location text");
static_assert(kQweatherApiUrlSize > kQweatherAlertUrlSize,
              "general QWeather API URL buffer must stay larger than alert URL buffer");
static_assert(kWeatherAlertEventNameSize > 1, "weather alert event name buffer must fit text and NUL");
static_assert(kWeatherAlertColorCodeSize > 1, "weather alert color code buffer must fit text and NUL");
static_assert(kWeatherAlertHeadlineSize <= kWeatherAlertTitleLen,
              "temporary alert headline must fit final alert title storage");
static_assert(kQweatherCityIdSize > 1, "QWeather city id buffer must fit text and NUL");
static_assert(kWeatherCityNameSize <= kManualWeatherCityLen,
              "QWeather city name must fit manual weather city storage");
constexpr int kQweatherDaily3DayEndpointDays = 3;
constexpr int kQweatherDaily7DayEndpointDays = 7;
static_assert(kQweatherDaily7DayEndpointDays > kQweatherDaily3DayEndpointDays,
              "QWeather 7-day endpoint must cover more days than 3-day endpoint");
static_assert(kWeatherForecastDays <= kQweatherDaily7DayEndpointDays,
              "stored forecast day count must fit the preferred QWeather endpoint");
constexpr const char *kQweatherCityLookupUrlFormat =
    "https://geoapi.qweather.com/v2/city/lookup?location=%s&number=1&range=cn&lang=zh";
constexpr const char *kQweatherAlertUrlFormat =
    "https://%s/weatheralert/v1/current/%s/%s?lang=zh&localTime=true";
constexpr const char *kQweatherNowUrlFormat =
    "https://%s/v7/weather/now?location=%s&lang=zh&unit=m";
constexpr const char *kQweatherDailyUrlFormat =
    "https://%s/v7/weather/%dd?location=%s&lang=zh&unit=m";
constexpr const char *kQweatherAirUrlFormat =
    "https://%s/v7/air/now?location=%s&lang=zh";
constexpr const char *kWeatherAlertEventColorFormat = "%s%s%s";
constexpr const char *kWeatherAlertEventOnlyFormat = "%s%s";
constexpr const char *kQweatherEndpointTexts[] = {
    kQweatherApiHost,
    kQweatherGeoApiHost,
    kQweatherCityLookupUrlFormat,
    kQweatherAlertUrlFormat,
    kQweatherNowUrlFormat,
    kQweatherDailyUrlFormat,
    kQweatherAirUrlFormat,
    kWeatherAlertSuffix,
    kWeatherAlertEventColorFormat,
    kWeatherAlertEventOnlyFormat,
};
constexpr const char *kQweatherStageCity = "city";
constexpr const char *kQweatherStageAlert = "alert";
constexpr const char *kQweatherStageNow = "now";
constexpr const char *kQweatherStageDaily = "daily";
constexpr const char *kQweatherStageAir = "air";
constexpr const char *kQweatherPreviewCityLabel = "qweather city";
constexpr const char *kQweatherPreviewAlertLabel = "qweather alert";
constexpr const char *kQweatherPreviewNowLabel = "qweather now";
constexpr const char *kQweatherPreviewDailyLabel = "qweather daily";
constexpr const char *kQweatherPreviewAirLabel = "qweather air";
constexpr const char *kQweatherUnknownStage = "unknown";
constexpr const char *kQweatherJsonLocationField = "location";
constexpr const char *kQweatherJsonIdField = "id";
constexpr const char *kQweatherJsonNameField = "name";
constexpr const char *kQweatherJsonLatField = "lat";
constexpr const char *kQweatherJsonLonField = "lon";
constexpr const char *kQweatherJsonNowField = "now";
constexpr const char *kQweatherNowJsonTextField = "text";
constexpr const char *kQweatherNowJsonIconField = "icon";
constexpr const char *kQweatherNowJsonTempField = "temp";
constexpr const char *kQweatherNowJsonHumidityField = "humidity";
constexpr const char *kQweatherAlertJsonAlertsField = "alerts";
constexpr const char *kQweatherAlertJsonEventTypeField = "eventType";
constexpr const char *kQweatherAlertJsonColorField = "color";
constexpr const char *kQweatherAlertJsonEventNameField = "name";
constexpr const char *kQweatherAlertJsonColorCodeField = "code";
constexpr const char *kQweatherAlertJsonHeadlineField = "headline";
constexpr const char *kQweatherDailyJsonDailyField = "daily";
constexpr const char *kQweatherAirJsonAqiField = "aqi";
constexpr const char *kQweatherAirJsonCategoryField = "category";
constexpr const char *kQweatherAirJsonPrimaryField = "primary";
constexpr const char *kQweatherAirJsonPm25Field = "pm2p5";
constexpr const char *kQweatherJsonFieldTexts[] = {
    kQweatherJsonLocationField,
    kQweatherJsonIdField,
    kQweatherJsonNameField,
    kQweatherJsonLatField,
    kQweatherJsonLonField,
    kQweatherJsonNowField,
    kQweatherNowJsonTextField,
    kQweatherNowJsonIconField,
    kQweatherNowJsonTempField,
    kQweatherNowJsonHumidityField,
    kQweatherAlertJsonAlertsField,
    kQweatherAlertJsonEventTypeField,
    kQweatherAlertJsonColorField,
    kQweatherAlertJsonEventNameField,
    kQweatherAlertJsonColorCodeField,
    kQweatherAlertJsonHeadlineField,
    kQweatherDailyJsonDailyField,
    kQweatherAirJsonAqiField,
    kQweatherAirJsonCategoryField,
    kQweatherAirJsonPrimaryField,
    kQweatherAirJsonPm25Field,
};
#define QWEATHER_URL_INVALID_ARG_FORMAT "qweather url invalid arg stage=%s"
#define QWEATHER_URL_TOO_LONG_FORMAT "qweather %s url too long"
constexpr const char *kQweatherCityInvalidArgLog = "qweather city invalid arg";
constexpr const char *kQweatherCityLocationTooLongLog = "qweather city location too long";
constexpr const char *kQweatherCityHttpFailedLog = "qweather city lookup http failed";
#define QWEATHER_CITY_LOOKUP_FORMAT "qweather city lookup: %s via %s"
#define QWEATHER_CITY_RESOLVED_FORMAT "qweather city resolved: %s id=%s"
#define QWEATHER_CITY_LOOKUP_FAILED_FORMAT "qweather city lookup failed code=%s"
constexpr const char *kQweatherAlertInvalidArgLog = "qweather alert invalid arg";
constexpr const char *kQweatherAlertHttpFailedLog = "qweather alert http failed";
constexpr const char *kQweatherAlertTitleFormatFailedLog = "qweather alert title format failed";
#define QWEATHER_ALERT_LOOKUP_FORMAT "qweather alert lookup: %s,%s via %s"
constexpr const char *kQweatherNowInvalidArgLog = "qweather now invalid arg";
constexpr const char *kQweatherNowLocationTooLongLog = "qweather now location too long";
constexpr const char *kQweatherNowHttpFailedLog = "qweather now http failed";
#define QWEATHER_NOW_LOOKUP_FORMAT "qweather now lookup: %s via %s"
#define QWEATHER_NOW_FAILED_FORMAT "qweather now failed code=%s"
constexpr const char *kQweatherDailyInvalidArgLog = "qweather daily invalid arg";
constexpr const char *kQweatherDailyLocationTooLongLog = "qweather daily location too long";
#define QWEATHER_DAILY_LOOKUP_FORMAT "qweather daily lookup: %s %dd via %s"
#define QWEATHER_DAILY_HTTP_FAILED_FORMAT "qweather daily http failed err=%s"
#define QWEATHER_DAILY_FAILED_FORMAT "qweather daily failed code=%s"
constexpr const char *kQweatherAirInvalidArgLog = "qweather air invalid arg";
constexpr const char *kQweatherAirLocationTooLongLog = "qweather air location too long";
#define QWEATHER_AIR_LOOKUP_FORMAT "qweather air lookup: %s via %s"
#define QWEATHER_AIR_HTTP_FAILED_FORMAT "qweather air http failed err=%s"
#define QWEATHER_AIR_FAILED_FORMAT "qweather air failed code=%s"
#define WEATHER_UPDATED_LOG_FORMAT "weather updated: %s %s %sC %s%% icon=%s forecast=%s air=%s"
constexpr const char *kWeatherFetchStatusOk = "ok";
constexpr const char *kWeatherFetchStatusCached = "cached";
#define WEATHER_UPDATE_MANUAL_CITY_FORMAT "weather update using manual city: %s"
#define WEATHER_MANUAL_CITY_LOOKUP_FAILED_FORMAT "manual weather city lookup failed: %s"
#define WEATHER_MANUAL_CITY_UPDATE_FAILED_FORMAT "weather update failed for manual city: %s"
#define WEATHER_RETRY_IP_CITY_LOOKUP_FORMAT "retry qweather city lookup by ip city: %s"
#define WEATHER_USING_IP_COORDINATES_FORMAT "using ip coordinates for weather now: %s"
constexpr const char *kWeatherIpLookupUpdateFailedLog = "weather update failed after ip lookup";
constexpr const char *kWeatherIpGeolocationLookupFailedLog = "ip geolocation lookup failed";
constexpr const char *kWeatherReadyEventUnavailableLog = "weather ready event skipped: app events unavailable";
constexpr const char *kQweatherFixedWarningTexts[] = {
    kQweatherCityInvalidArgLog,
    kQweatherCityLocationTooLongLog,
    kQweatherCityHttpFailedLog,
    kQweatherAlertInvalidArgLog,
    kQweatherAlertHttpFailedLog,
    kQweatherAlertTitleFormatFailedLog,
    kQweatherNowInvalidArgLog,
    kQweatherNowLocationTooLongLog,
    kQweatherNowHttpFailedLog,
    kQweatherDailyInvalidArgLog,
    kQweatherDailyLocationTooLongLog,
    kQweatherAirInvalidArgLog,
    kQweatherAirLocationTooLongLog,
    kWeatherReadyEventUnavailableLog,
};
constexpr const char *kQweatherStageAndStatusTexts[] = {
    kQweatherStageCity,
    kQweatherStageAlert,
    kQweatherStageNow,
    kQweatherStageDaily,
    kQweatherStageAir,
    kQweatherPreviewCityLabel,
    kQweatherPreviewAlertLabel,
    kQweatherPreviewNowLabel,
    kQweatherPreviewDailyLabel,
    kQweatherPreviewAirLabel,
    kQweatherUnknownStage,
    kWeatherFetchStatusOk,
    kWeatherFetchStatusCached,
    kWeatherIpLookupUpdateFailedLog,
    kWeatherIpGeolocationLookupFailedLog,
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&texts)[N])
{
    for (const char *text : texts) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

constexpr bool qweather_json_field_texts_nonempty()
{
    return cstr_array_nonempty(kQweatherJsonFieldTexts);
}

constexpr bool qweather_stage_and_status_texts_nonempty()
{
    return cstr_array_nonempty(kQweatherStageAndStatusTexts);
}

constexpr bool qweather_fixed_warning_texts_nonempty()
{
    return cstr_array_nonempty(kQweatherFixedWarningTexts);
}

constexpr bool qweather_endpoint_texts_nonempty()
{
    return cstr_array_nonempty(kQweatherEndpointTexts);
}

static_assert(array_count(kQweatherJsonFieldTexts) > 0,
              "QWeather JSON field guard must cover parsed fields");
static_assert(qweather_json_field_texts_nonempty(),
              "QWeather JSON field and business code strings must be non-empty");
static_assert(array_count(kQweatherStageAndStatusTexts) > 0,
              "QWeather stage/status guard must cover stage and status text");
static_assert(array_count(kQweatherFixedWarningTexts) > 0,
              "QWeather fixed warning guard must cover fixed warning text");
static_assert(array_count(kQweatherEndpointTexts) > 0,
              "QWeather endpoint guard must cover endpoint text");
static_assert(qweather_stage_and_status_texts_nonempty(),
              "QWeather stage, preview, status and fixed log texts must be non-empty");
static_assert(qweather_fixed_warning_texts_nonempty(),
              "QWeather fixed warning texts must be non-empty");
static_assert(qweather_endpoint_texts_nonempty(),
              "QWeather endpoint, location and alert texts must be non-empty");

void log_qweather_fixed_warning(const char *message);

bool qweather_output_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

bool qweather_format_failed(int written, size_t out_len)
{
    return written < 0 || (size_t)written >= out_len;
}

bool format_qweather_url(char *out, size_t out_len, const char *stage, const char *fmt, ...)
{
    if (!qweather_output_available(out, out_len) || !fmt) {
        ESP_LOGW(TAG, QWEATHER_URL_INVALID_ARG_FORMAT, qweather_stage_text(stage));
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out, out_len, fmt, args);
    va_end(args);
    if (qweather_format_failed(written, out_len)) {
        out[0] = '\0';
        ESP_LOGW(TAG, QWEATHER_URL_TOO_LONG_FORMAT, qweather_stage_text(stage));
        return false;
    }
    return true;
}

bool encode_qweather_location_or_warn(const char *location, char *out, size_t out_len, const char *warning)
{
    if (url_encode_component(location, out, out_len)) {
        return true;
    }
    log_qweather_fixed_warning(warning);
    return false;
}

bool format_qweather_api_url_for_city(char *url,
                                      size_t url_len,
                                      const char *stage,
                                      const char *url_format,
                                      const char *city_id,
                                      const char *location_warning)
{
    char encoded_location[kQweatherEncodedLocationSize] = {};
    if (!encode_qweather_location_or_warn(city_id,
                                          encoded_location,
                                          sizeof(encoded_location),
                                          location_warning)) {
        return false;
    }
    return format_qweather_url(url,
                               url_len,
                               stage,
                               url_format,
                               qweather_api_host(),
                               encoded_location);
}

bool format_qweather_daily_url_for_city(char *url,
                                        size_t url_len,
                                        const char *city_id,
                                        int days)
{
    char encoded_location[kQweatherEncodedLocationSize] = {};
    if (!encode_qweather_location_or_warn(city_id,
                                          encoded_location,
                                          sizeof(encoded_location),
                                          kQweatherDailyLocationTooLongLog)) {
        return false;
    }
    return format_qweather_url(url,
                               url_len,
                               kQweatherStageDaily,
                               kQweatherDailyUrlFormat,
                               qweather_api_host(),
                               days,
                               encoded_location);
}

void log_qweather_fixed_warning(const char *message)
{
    ESP_LOGW(TAG, "%s", cstr_nonempty(message) ? message : kQweatherUnknownStage);
}

} // namespace

QweatherCityLookupStatus qweather_lookup_city_status(const char *location,
                                                      char *city_id,
                                                      size_t city_id_len,
                                                      char *city_name,
                                                      size_t city_name_len,
                                                      char *lat_out,
                                                      size_t lat_len,
                                                      char *lon_out,
                                                      size_t lon_len)
{
    if (!location ||
        !qweather_output_available(city_id, city_id_len) ||
        !qweather_output_available(city_name, city_name_len)) {
        log_qweather_fixed_warning(kQweatherCityInvalidArgLog);
        return kQweatherCityLookupError;
    }
    char encoded_location[kQweatherEncodedLocationSize] = {};
    if (!encode_qweather_location_or_warn(location,
                                          encoded_location,
                                          sizeof(encoded_location),
                                          kQweatherCityLocationTooLongLog)) {
        return kQweatherCityLookupNotFound;
    }

    char url[kQweatherApiUrlSize] = {};
    if (!format_qweather_url(url,
                             sizeof(url),
                             kQweatherStageCity,
                             kQweatherCityLookupUrlFormat,
                             encoded_location)) {
        return kQweatherCityLookupError;
    }
    ESP_LOGI(TAG, QWEATHER_CITY_LOOKUP_FORMAT, location, kQweatherGeoApiHost);
    QweatherResponseBuffer response(kQweatherStageCity, kQweatherCityResponseBufferSize);
    if (!response) {
        return kQweatherCityLookupError;
    }
    if (http_get_text(url, response.get(), response.size(), g_weather_api_key) != ESP_OK) {
        log_qweather_fixed_warning(kQweatherCityHttpFailedLog);
        return kQweatherCityLookupError;
    }
    QweatherJsonRoot root(response.get());
    if (!root) {
        log_response_preview(kQweatherPreviewCityLabel, response.get());
        return kQweatherCityLookupError;
    }
    bool ok = false;
    QweatherCityLookupStatus status = kQweatherCityLookupNotFound;
    const cJSON *code = nullptr;
    const cJSON *locations = qweather_success_array(root.get(), kQweatherJsonLocationField, &code);
    const cJSON *first = cJSON_IsArray(locations) ? cJSON_GetArrayItem(locations, 0) : nullptr;
    if (cJSON_IsObject(first)) {
        ok = json_copy_string(first, kQweatherJsonIdField, city_id, city_id_len) &&
             json_copy_string(first, kQweatherJsonNameField, city_name, city_name_len);
        if (ok) {
            if (lat_out && lat_len > 0) {
                json_copy_string(first, kQweatherJsonLatField, lat_out, lat_len);
            }
            if (lon_out && lon_len > 0) {
                json_copy_string(first, kQweatherJsonLonField, lon_out, lon_len);
            }
            ESP_LOGI(TAG, QWEATHER_CITY_RESOLVED_FORMAT, city_name, city_id);
        }
        status = ok ? kQweatherCityLookupOk : kQweatherCityLookupError;
    } else {
        ESP_LOGW(TAG, QWEATHER_CITY_LOOKUP_FAILED_FORMAT, qweather_code_text(code));
    }
    return status;
}

bool qweather_lookup_city(const char *location,
                          char *city_id,
                          size_t city_id_len,
                          char *city_name,
                          size_t city_name_len,
                          char *lat_out,
                          size_t lat_len,
                          char *lon_out,
                          size_t lon_len)
{
    return qweather_lookup_city_status(location,
                                       city_id,
                                       city_id_len,
                                       city_name,
                                       city_name_len,
                                       lat_out,
                                       lat_len,
                                       lon_out,
                                       lon_len) == kQweatherCityLookupOk;
}

static bool lookup_weather_city(const char *location,
                                char *city_id,
                                char *city_name,
                                WeatherData *weather)
{
    if (!city_id || !city_name || !weather) {
        return false;
    }
    return qweather_lookup_city(location,
                                city_id,
                                kQweatherCityIdSize,
                                city_name,
                                kWeatherCityNameSize,
                                weather->lat,
                                sizeof(weather->lat),
                                weather->lon,
                                sizeof(weather->lon));
}

static void format_weather_alert_title_text(char *title,
                                            size_t title_len,
                                            const char *format,
                                            const char *event_name,
                                            const char *color_name = nullptr)
{
    if (!qweather_output_available(title, title_len) || !format) {
        return;
    }
    int written = 0;
    if (color_name) {
        written = snprintf(title, title_len, format, event_name, color_name, kWeatherAlertSuffix);
    } else {
        written = snprintf(title, title_len, format, event_name, kWeatherAlertSuffix);
    }
    if (written < 0) {
        title[0] = '\0';
        log_qweather_fixed_warning(kQweatherAlertTitleFormatFailedLog);
    }
}

static void build_weather_alert_title(char *title,
                                      size_t title_len,
                                      const char *event_name,
                                      const char *color_code,
                                      const char *headline)
{
    if (!qweather_output_available(title, title_len)) {
        return;
    }
    title[0] = '\0';
    const char *color_name = warning_color_name(color_code);
    if (cstr_nonempty(event_name) && cstr_nonempty(color_name)) {
        format_weather_alert_title_text(title, title_len, kWeatherAlertEventColorFormat, event_name, color_name);
    } else if (cstr_nonempty(headline)) {
        strlcpy(title, headline, title_len);
    } else if (cstr_nonempty(event_name)) {
        format_weather_alert_title_text(title, title_len, kWeatherAlertEventOnlyFormat, event_name);
    }
}

static void parse_weather_alert_item(const cJSON *item, WeatherAlertData *alert)
{
    if (!cJSON_IsObject(item) || !alert) {
        return;
    }
    char event_name[kWeatherAlertEventNameSize] = {};
    char color_code[kWeatherAlertColorCodeSize] = {};
    char headline[kWeatherAlertHeadlineSize] = {};
    const cJSON *event = cJSON_GetObjectItem(item, kQweatherAlertJsonEventTypeField);
    const cJSON *color = cJSON_GetObjectItem(item, kQweatherAlertJsonColorField);
    if (event) {
        json_copy_string(event, kQweatherAlertJsonEventNameField, event_name, sizeof(event_name));
    }
    if (color) {
        json_copy_string(color, kQweatherAlertJsonColorCodeField, color_code, sizeof(color_code));
    }
    json_copy_string(item, kQweatherAlertJsonHeadlineField, headline, sizeof(headline));

    int rank = warning_color_rank(color_code);
    char title[kWeatherAlertTitleLen] = {};
    build_weather_alert_title(title, sizeof(title), event_name, color_code, headline);
    add_weather_alert_title(alert, title, rank);
}

bool qweather_fetch_alert(const char *lat, const char *lon, WeatherAlertData *alert)
{
    if (!alert) {
        log_qweather_fixed_warning(kQweatherAlertInvalidArgLog);
        return false;
    }
    if (!lat || !lon || lat[0] == '\0' || lon[0] == '\0') {
        alert->active = false;
        return true;
    }

    const char *host = qweather_api_host();
    char url[kQweatherAlertUrlSize] = {};
    if (!format_qweather_url(url,
                             sizeof(url),
                             kQweatherStageAlert,
                             kQweatherAlertUrlFormat,
                             host,
                             lat,
                             lon)) {
        return false;
    }
    ESP_LOGI(TAG, QWEATHER_ALERT_LOOKUP_FORMAT, lat, lon, host);
    QweatherResponseBuffer response(kQweatherStageAlert, kQweatherAlertResponseBufferSize);
    if (!response) {
        return false;
    }
    if (http_get_text(url, response.get(), response.size(), g_weather_api_key) != ESP_OK) {
        log_qweather_fixed_warning(kQweatherAlertHttpFailedLog);
        return false;
    }
    QweatherJsonRoot root(response.get());
    if (!root) {
        log_response_preview(kQweatherPreviewAlertLabel, response.get());
        return false;
    }

    WeatherAlertData next = {};
    const cJSON *alerts = cJSON_GetObjectItem(root.get(), kQweatherAlertJsonAlertsField);
    int alert_count = cJSON_IsArray(alerts) ? cJSON_GetArraySize(alerts) : 0;
    for (int i = 0; i < alert_count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(alerts, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }
        parse_weather_alert_item(item, &next);
    }
    next.active = next.count > 0;
    time(&next.updated_at);
    *alert = next;

    return true;
}

static bool parse_weather_now(const cJSON *now, WeatherData *weather)
{
    if (!cJSON_IsObject(now) || !weather) {
        return false;
    }
    return json_copy_string(now, kQweatherNowJsonTextField, weather->text, sizeof(weather->text)) &&
           json_copy_string(now, kQweatherNowJsonIconField, weather->icon, sizeof(weather->icon)) &&
           json_copy_string(now, kQweatherNowJsonTempField, weather->temp, sizeof(weather->temp)) &&
           json_copy_string(now, kQweatherNowJsonHumidityField, weather->humidity, sizeof(weather->humidity));
}

bool qweather_fetch_now(const char *city_id, WeatherData *weather)
{
    if (!city_id || !weather) {
        log_qweather_fixed_warning(kQweatherNowInvalidArgLog);
        return false;
    }
    const char *host = qweather_api_host();
    char url[kQweatherApiUrlSize] = {};
    if (!format_qweather_api_url_for_city(url,
                                          sizeof(url),
                                          kQweatherStageNow,
                                          kQweatherNowUrlFormat,
                                          city_id,
                                          kQweatherNowLocationTooLongLog)) {
        return false;
    }
    ESP_LOGI(TAG, QWEATHER_NOW_LOOKUP_FORMAT, city_id, host);
    QweatherResponseBuffer response(kQweatherStageNow, kQweatherNowResponseBufferSize);
    if (!response) {
        return false;
    }
    if (http_get_text(url, response.get(), response.size(), g_weather_api_key) != ESP_OK) {
        log_qweather_fixed_warning(kQweatherNowHttpFailedLog);
        return false;
    }
    QweatherJsonRoot root(response.get());
    if (!root) {
        log_response_preview(kQweatherPreviewNowLabel, response.get());
        return false;
    }
    bool ok = false;
    const cJSON *code = nullptr;
    const cJSON *now = qweather_success_object(root.get(), kQweatherJsonNowField, &code);
    if (now) {
        ok = parse_weather_now(now, weather);
    } else {
        ESP_LOGW(TAG, QWEATHER_NOW_FAILED_FORMAT, qweather_code_text(code));
    }
    return ok;
}

static bool qweather_fetch_daily_days(const char *city_id, int days, WeatherForecastData *forecast)
{
    if (!city_id || !forecast ||
        (days != kQweatherDaily3DayEndpointDays && days != kQweatherDaily7DayEndpointDays)) {
        log_qweather_fixed_warning(kQweatherDailyInvalidArgLog);
        return false;
    }
    const char *host = qweather_api_host();
    char url[kQweatherApiUrlSize] = {};
    if (!format_qweather_daily_url_for_city(url, sizeof(url), city_id, days)) {
        return false;
    }
    ESP_LOGI(TAG, QWEATHER_DAILY_LOOKUP_FORMAT, city_id, days, host);
    QweatherResponseBuffer response(kQweatherStageDaily, kQweatherDailyResponseBufferSize);
    if (!response) {
        return false;
    }
    esp_err_t http_err = http_get_text(url, response.get(), response.size(), g_weather_api_key);
    if (http_err != ESP_OK) {
        ESP_LOGW(TAG, QWEATHER_DAILY_HTTP_FAILED_FORMAT, esp_err_to_name(http_err));
        return false;
    }
    QweatherJsonRoot root(response.get());
    if (!root) {
        log_response_preview(kQweatherPreviewDailyLabel, response.get());
        return false;
    }

    WeatherForecastData next = {};
    bool ok = false;
    const cJSON *code = nullptr;
    const cJSON *daily = qweather_success_array(root.get(), kQweatherDailyJsonDailyField, &code);
    if (daily) {
        if (parse_qweather_forecast_days(daily, &next)) {
            *forecast = next;
            ok = true;
        }
    } else {
        ESP_LOGW(TAG, QWEATHER_DAILY_FAILED_FORMAT, qweather_code_text(code));
    }
    return ok;
}

bool qweather_fetch_daily(const char *city_id, WeatherForecastData *forecast)
{
    if (qweather_fetch_daily_days(city_id, kQweatherDaily7DayEndpointDays, forecast)) {
        return true;
    }
    return qweather_fetch_daily_days(city_id, kQweatherDaily3DayEndpointDays, forecast);
}

static bool copy_weather_air_required_fields(const cJSON *now, WeatherAirData *air)
{
    return json_copy_string(now, kQweatherAirJsonAqiField, air->aqi, sizeof(air->aqi)) &&
           json_copy_string(now, kQweatherAirJsonCategoryField, air->category, sizeof(air->category));
}

static void copy_weather_air_optional_fields(const cJSON *now, WeatherAirData *air)
{
    json_copy_string(now, kQweatherAirJsonPrimaryField, air->primary, sizeof(air->primary));
    json_copy_string(now, kQweatherAirJsonPm25Field, air->pm2p5, sizeof(air->pm2p5));
}

static bool parse_weather_air(const cJSON *now, WeatherAirData *air)
{
    if (!cJSON_IsObject(now) || !air) {
        return false;
    }
    bool ok = copy_weather_air_required_fields(now, air);
    copy_weather_air_optional_fields(now, air);
    return ok;
}

bool qweather_fetch_air(const char *city_id, WeatherAirData *air)
{
    if (!city_id || !air) {
        log_qweather_fixed_warning(kQweatherAirInvalidArgLog);
        return false;
    }
    const char *host = qweather_api_host();
    char url[kQweatherApiUrlSize] = {};
    if (!format_qweather_api_url_for_city(url,
                                          sizeof(url),
                                          kQweatherStageAir,
                                          kQweatherAirUrlFormat,
                                          city_id,
                                          kQweatherAirLocationTooLongLog)) {
        return false;
    }
    ESP_LOGI(TAG, QWEATHER_AIR_LOOKUP_FORMAT, city_id, host);
    QweatherResponseBuffer response(kQweatherStageAir, kQweatherAirResponseBufferSize);
    if (!response) {
        return false;
    }
    esp_err_t http_err = http_get_text(url, response.get(), response.size(), g_weather_api_key);
    if (http_err != ESP_OK) {
        ESP_LOGW(TAG, QWEATHER_AIR_HTTP_FAILED_FORMAT, esp_err_to_name(http_err));
        return false;
    }

    QweatherJsonRoot root(response.get());
    if (!root) {
        log_response_preview(kQweatherPreviewAirLabel, response.get());
        return false;
    }
    WeatherAirData next = {};
    bool ok = false;
    const cJSON *code = nullptr;
    const cJSON *now = qweather_success_object(root.get(), kQweatherJsonNowField, &code);
    if (now) {
        ok = parse_weather_air(now, &next);
        next.ready = ok;
        if (ok) {
            time(&next.updated_at);
            *air = next;
        }
    } else {
        ESP_LOGW(TAG, QWEATHER_AIR_FAILED_FORMAT, qweather_code_text(code));
    }
    return ok;
}

void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    portENTER_CRITICAL(&g_weather_state_mux);
    if (weather) {
        *weather = g_weather;
    }
    if (alert) {
        *alert = g_weather_alert;
    }
    if (forecast) {
        *forecast = g_weather_forecast;
    }
    if (air) {
        *air = g_weather_air;
    }
    portEXIT_CRITICAL(&g_weather_state_mux);
}

void get_weather_snapshot(WeatherData *weather, WeatherAlertData *alert)
{
    get_weather_full_snapshot(weather, alert, nullptr, nullptr);
}

void get_weather_forecast_snapshot(WeatherForecastData *forecast)
{
    if (!forecast) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, forecast, nullptr);
}

void get_weather_air_snapshot(WeatherAirData *air)
{
    if (!air) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, nullptr, air);
}

static void publish_weather_ready_event()
{
    if (!g_app_events) {
        log_qweather_fixed_warning(kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kWeatherReadyBit);
}

static void clear_weather_ready_event()
{
    if (!g_app_events) {
        log_qweather_fixed_warning(kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupClearBits(g_app_events, kWeatherReadyBit);
}

static void commit_weather_update_snapshot(const WeatherData &next,
                                           const WeatherAlertData &next_alert,
                                           const WeatherForecastData &next_forecast,
                                           const WeatherAirData &next_air,
                                           bool forecast_ok,
                                           bool air_ok)
{
    time_t now = 0;
    time(&now);
    portENTER_CRITICAL(&g_weather_state_mux);
    g_weather = next;
    g_weather_alert = next_alert;
    if (forecast_ok) {
        g_weather_forecast = next_forecast;
    }
    if (air_ok) {
        g_weather_air = next_air;
    }
    g_last_weather_sync_time = now;
    portEXIT_CRITICAL(&g_weather_state_mux);
    publish_weather_ready_event();
    ESP_LOGI(TAG, WEATHER_UPDATED_LOG_FORMAT,
             next.city,
             next.text,
             next.temp,
             next.humidity,
             next.icon,
             forecast_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached,
             air_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached);
}

static bool fetch_and_commit_weather(const char *city_id, WeatherData *next)
{
    if (!city_id || !next) {
        return false;
    }
    if (!qweather_fetch_now(city_id, next)) {
        return false;
    }

    WeatherAlertData next_alert = {};
    WeatherForecastData next_forecast = {};
    WeatherAirData next_air = {};
    (void)qweather_fetch_alert(next->lat, next->lon, &next_alert);
    bool forecast_ok = qweather_fetch_daily(city_id, &next_forecast);
    bool air_ok = qweather_fetch_air(city_id, &next_air);
    commit_weather_update_snapshot(*next, next_alert, next_forecast, next_air, forecast_ok, air_ok);
    return true;
}

static bool update_weather_by_manual_city(const char *manual_city)
{
    char city_id[kQweatherCityIdSize] = {};
    char lookup_city[kWeatherCityNameSize] = {};
    WeatherData next = {};

    ESP_LOGI(TAG, WEATHER_UPDATE_MANUAL_CITY_FORMAT, manual_city);
    bool have_city_id = lookup_weather_city(manual_city, city_id, lookup_city, &next);
    if (!have_city_id) {
        ESP_LOGW(TAG, WEATHER_MANUAL_CITY_LOOKUP_FAILED_FORMAT, manual_city);
        return false;
    }
    copy_first_nonempty_text(next.city, sizeof(next.city), lookup_city, manual_city);
    if (fetch_and_commit_weather(city_id, &next)) {
        return true;
    }
    ESP_LOGW(TAG, WEATHER_MANUAL_CITY_UPDATE_FAILED_FORMAT, manual_city);
    return false;
}

static bool update_weather_by_ip_location()
{
    char location[kWeatherLocationTextSize] = {};
    char city_id[kQweatherCityIdSize] = {};
    char ip_city[kWeatherCityNameSize] = {};
    char lookup_city[kWeatherCityNameSize] = {};
    WeatherData next = {};

    if (!ip_geolocation_lookup(location, sizeof(location), ip_city, sizeof(ip_city))) {
        log_qweather_fixed_warning(kWeatherIpGeolocationLookupFailedLog);
        return false;
    }
    trim_ascii(location);
    bool have_city_id = lookup_weather_city(location, city_id, lookup_city, &next);
    if (!have_city_id && ip_city[0] != '\0') {
        ESP_LOGW(TAG, WEATHER_RETRY_IP_CITY_LOOKUP_FORMAT, ip_city);
        have_city_id = lookup_weather_city(ip_city, city_id, lookup_city, &next);
    }
    copy_first_nonempty_text(next.city, sizeof(next.city), ip_city, lookup_city, location);
    if (!have_city_id) {
        copy_ip_coordinate_location(location, city_id, sizeof(city_id), &next);
        ESP_LOGW(TAG, WEATHER_USING_IP_COORDINATES_FORMAT, city_id);
    }
    if (fetch_and_commit_weather(city_id, &next)) {
        return true;
    }
    log_qweather_fixed_warning(kWeatherIpLookupUpdateFailedLog);
    return false;
}

bool perform_weather_update()
{
    if (!g_have_weather_key || g_low_battery_mode) {
        clear_weather_ready_event();
        return false;
    }

    char manual_city[kManualWeatherCityLen] = {};
    if (g_has_manual_weather_city) {
        strlcpy(manual_city, g_manual_weather_city, sizeof(manual_city));
        trim_ascii(manual_city);
    }
    if (manual_city[0] != '\0') {
        return update_weather_by_manual_city(manual_city);
    }
    return update_weather_by_ip_location();
}
