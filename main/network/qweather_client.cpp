// 对接 IP 定位、QWeather 城市查询、实时天气和天气预警接口。
#include "network_services.h"

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
constexpr size_t kIpGeoResponseBufferSize = 2048;
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
constexpr size_t kIpRegionCopySize = 96;
constexpr size_t kWeatherLocationTextSize = 32;
constexpr size_t kQweatherCityIdSize = 24;
constexpr size_t kWeatherCityNameSize = 32;
constexpr size_t kWeatherIconUtf8TextSize = 5;
static_assert(kIpGeoResponseBufferSize > 1, "IP geolocation response buffer must fit text and NUL");
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
static_assert(kIpRegionCopySize > kWeatherLocationTextSize,
              "IP region scratch buffer must be larger than displayed location text");
static_assert(kWeatherLocationTextSize <= kManualWeatherCityLen,
              "weather location text must fit manual weather city storage");
static_assert(kQweatherCityIdSize > 1, "QWeather city id buffer must fit text and NUL");
static_assert(kWeatherCityNameSize <= kManualWeatherCityLen,
              "QWeather city name must fit manual weather city storage");
constexpr uint32_t kWeatherIconDefaultCodepoint = 0xF146;
constexpr int kWeatherIconSunnyStart = 100;
constexpr int kWeatherIconSunnyEnd = 104;
constexpr uint32_t kWeatherIconSunnyBaseCodepoint = 0xF101;
constexpr int kWeatherIconNightSunnyStart = 150;
constexpr int kWeatherIconNightSunnyEnd = 153;
constexpr uint32_t kWeatherIconNightSunnyBaseCodepoint = 0xF106;
constexpr int kWeatherIconRainStart = 300;
constexpr int kWeatherIconRainEnd = 318;
constexpr uint32_t kWeatherIconRainBaseCodepoint = 0xF10A;
constexpr int kWeatherIconNightRainStart = 350;
constexpr int kWeatherIconNightRainEnd = 351;
constexpr uint32_t kWeatherIconNightRainBaseCodepoint = 0xF11D;
constexpr int kWeatherIconRainUnknownCode = 399;
constexpr uint32_t kWeatherIconRainUnknownCodepoint = 0xF11F;
constexpr int kWeatherIconSnowStart = 400;
constexpr int kWeatherIconSnowEnd = 410;
constexpr uint32_t kWeatherIconSnowBaseCodepoint = 0xF120;
constexpr int kWeatherIconNightSnowStart = 456;
constexpr int kWeatherIconNightSnowEnd = 457;
constexpr uint32_t kWeatherIconNightSnowBaseCodepoint = 0xF12B;
constexpr int kWeatherIconSnowUnknownCode = 499;
constexpr uint32_t kWeatherIconSnowUnknownCodepoint = 0xF12D;
constexpr int kWeatherIconFogStart = 500;
constexpr int kWeatherIconFogEnd = 504;
constexpr uint32_t kWeatherIconFogBaseCodepoint = 0xF12E;
constexpr int kWeatherIconDustStart = 507;
constexpr int kWeatherIconDustEnd = 515;
constexpr uint32_t kWeatherIconDustBaseCodepoint = 0xF133;
constexpr int kWeatherIconCloudStart = 800;
constexpr int kWeatherIconCloudEnd = 807;
constexpr uint32_t kWeatherIconCloudBaseCodepoint = 0xF13C;
constexpr int kWeatherIconHotCode = 900;
constexpr uint32_t kWeatherIconHotCodepoint = 0xF144;
constexpr int kWeatherIconColdCode = 901;
constexpr uint32_t kWeatherIconColdCodepoint = 0xF145;
constexpr int kWeatherIconUnknownCode = 9999;
constexpr uint32_t kWeatherIconUnknownCodepoint = 0xF1CB;
static_assert(kWeatherIconSunnyStart <= kWeatherIconSunnyEnd,
              "sunny weather icon range must be ordered");
static_assert(kWeatherIconNightSunnyStart <= kWeatherIconNightSunnyEnd,
              "night sunny weather icon range must be ordered");
static_assert(kWeatherIconRainStart <= kWeatherIconRainEnd,
              "rain weather icon range must be ordered");
static_assert(kWeatherIconNightRainStart <= kWeatherIconNightRainEnd,
              "night rain weather icon range must be ordered");
static_assert(kWeatherIconSnowStart <= kWeatherIconSnowEnd,
              "snow weather icon range must be ordered");
static_assert(kWeatherIconNightSnowStart <= kWeatherIconNightSnowEnd,
              "night snow weather icon range must be ordered");
static_assert(kWeatherIconFogStart <= kWeatherIconFogEnd,
              "fog weather icon range must be ordered");
static_assert(kWeatherIconDustStart <= kWeatherIconDustEnd,
              "dust weather icon range must be ordered");
static_assert(kWeatherIconCloudStart <= kWeatherIconCloudEnd,
              "cloud weather icon range must be ordered");
constexpr size_t kIpRegionMaxParts = 5;
constexpr size_t kIpRegionCityPartIndex = 2;
constexpr size_t kIpRegionCityPartMinCount = kIpRegionCityPartIndex + 1;
constexpr size_t kWeatherAlertCompactTitleChars = 6;
constexpr unsigned char kUtf8AsciiMask = 0x80;
constexpr unsigned char kUtf8TwoByteMask = 0xE0;
constexpr unsigned char kUtf8TwoBytePrefix = 0xC0;
constexpr unsigned char kUtf8ThreeByteMask = 0xF0;
constexpr unsigned char kUtf8ThreeBytePrefix = 0xE0;
constexpr unsigned char kUtf8FourByteMask = 0xF8;
constexpr unsigned char kUtf8FourBytePrefix = 0xF0;
constexpr uint32_t kUtf8OneByteMaxCodepoint = 0x7F;
constexpr uint32_t kUtf8TwoByteMaxCodepoint = 0x7FF;
constexpr uint32_t kUtf8ThreeByteMaxCodepoint = 0xFFFF;
constexpr unsigned char kUtf8ContinuationPrefix = 0x80;
constexpr uint32_t kUtf8ContinuationPayloadMask = 0x3F;
constexpr int kUtf8Shift6 = 6;
constexpr int kUtf8Shift12 = 12;
constexpr int kUtf8Shift18 = 18;
constexpr size_t kUtf8OneByteLen = 1;
constexpr size_t kUtf8TwoByteLen = 2;
constexpr size_t kUtf8ThreeByteLen = 3;
constexpr size_t kUtf8FourByteLen = 4;
static_assert(kWeatherIconUtf8TextSize > kUtf8FourByteLen,
              "weather icon UTF-8 text buffer must fit a four-byte codepoint and NUL");
static_assert(kUtf8OneByteLen < kUtf8TwoByteLen &&
                  kUtf8TwoByteLen < kUtf8ThreeByteLen &&
                  kUtf8ThreeByteLen < kUtf8FourByteLen,
              "UTF-8 byte length constants must stay ordered");
static_assert(kUtf8OneByteMaxCodepoint < kUtf8TwoByteMaxCodepoint &&
                  kUtf8TwoByteMaxCodepoint < kUtf8ThreeByteMaxCodepoint,
              "UTF-8 codepoint limits must stay ordered");
static_assert(kIpRegionCityPartIndex < kIpRegionMaxParts,
              "IP region city index must fit region parts array");
static_assert(kIpRegionCityPartMinCount <= kIpRegionMaxParts,
              "IP region city part minimum count must fit region parts array");
static_assert(kWeatherAlertCompactTitleChars > 0,
              "compact weather alert title length must be positive");
constexpr int kQweatherDaily3DayEndpointDays = 3;
constexpr int kQweatherDaily7DayEndpointDays = 7;
static_assert(kQweatherDaily3DayEndpointDays > 0, "QWeather 3-day endpoint day count must be positive");
static_assert(kQweatherDaily7DayEndpointDays > kQweatherDaily3DayEndpointDays,
              "QWeather 7-day endpoint must cover more days than 3-day endpoint");
static_assert(kWeatherForecastDays <= kQweatherDaily7DayEndpointDays,
              "stored forecast day count must fit the preferred QWeather endpoint");
constexpr int kWeatherAdviceHotTempC = 30;
constexpr int kWeatherAdviceColdTempC = 8;
constexpr int kWeatherAdviceLargeTempGapC = 10;
static_assert(kWeatherAdviceColdTempC < kWeatherAdviceHotTempC,
              "weather advice cold threshold must be below hot threshold");
static_assert(kWeatherAdviceLargeTempGapC > 0, "weather advice temperature gap must be positive");
constexpr const char *kWeatherAdviceRainOrSnow = "有雨雪，出门记得带伞。";
constexpr const char *kWeatherAdviceHot = "天气较热，注意防晒补水。";
constexpr const char *kWeatherAdviceCold = "气温偏低，注意保暖。";
constexpr const char *kWeatherAdviceLargeTempGap = "早晚温差大，建议备外套。";
constexpr const char *kWeatherAdviceCalm = "天气平稳，适合轻装出行。";
constexpr const char *kIpGeolocationUrl = "https://uapis.cn/api/v1/network/myip";
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
constexpr const char *kIpGeoCoordinateFormat = "%.4f,%.4f";
constexpr const char *kIpRegionDelimiter = " ";
constexpr const char *kIpGeoCitySuffix = "市";
constexpr const char *kWeatherAlertSuffix = "预警";
constexpr const char *kWeatherAlertEventColorFormat = "%s%s%s";
constexpr const char *kWeatherAlertEventOnlyFormat = "%s%s";
constexpr const char *kQweatherEndpointAndAdviceTexts[] = {
    kQweatherApiHost,
    kQweatherGeoApiHost,
    kIpGeolocationUrl,
    kQweatherCityLookupUrlFormat,
    kQweatherAlertUrlFormat,
    kQweatherNowUrlFormat,
    kQweatherDailyUrlFormat,
    kQweatherAirUrlFormat,
    kIpGeoCoordinateFormat,
    kIpRegionDelimiter,
    kIpGeoCitySuffix,
    kWeatherAlertSuffix,
    kWeatherAlertEventColorFormat,
    kWeatherAlertEventOnlyFormat,
    kWeatherAdviceRainOrSnow,
    kWeatherAdviceHot,
    kWeatherAdviceCold,
    kWeatherAdviceLargeTempGap,
    kWeatherAdviceCalm,
};
constexpr const char *kQweatherDefaultStage = "request";
constexpr const char *kQweatherStageIpLocation = "ip location";
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
constexpr const char *kIpGeoJsonLatitudeField = "latitude";
constexpr const char *kIpGeoJsonLongitudeField = "longitude";
constexpr const char *kIpGeoJsonRegionField = "region";
constexpr const char *kQweatherJsonCodeField = "code";
constexpr const char *kQweatherSuccessCode = "200";
constexpr const char *kQweatherMissingCodeText = "missing";
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
constexpr const char *kQweatherDailyJsonDateField = "fxDate";
constexpr const char *kQweatherDailyJsonTextDayField = "textDay";
constexpr const char *kQweatherDailyJsonIconDayField = "iconDay";
constexpr const char *kQweatherDailyJsonTempMaxField = "tempMax";
constexpr const char *kQweatherDailyJsonTempMinField = "tempMin";
constexpr const char *kQweatherDailyJsonHumidityField = "humidity";
constexpr const char *kQweatherDailyJsonWindDirDayField = "windDirDay";
constexpr const char *kQweatherDailyJsonWindScaleDayField = "windScaleDay";
constexpr const char *kQweatherDailyJsonSunriseField = "sunrise";
constexpr const char *kQweatherDailyJsonSunsetField = "sunset";
constexpr const char *kQweatherAirJsonAqiField = "aqi";
constexpr const char *kQweatherAirJsonCategoryField = "category";
constexpr const char *kQweatherAirJsonPrimaryField = "primary";
constexpr const char *kQweatherAirJsonPm25Field = "pm2p5";
constexpr const char *kQweatherJsonFieldTexts[] = {
    kIpGeoJsonLatitudeField,
    kIpGeoJsonLongitudeField,
    kIpGeoJsonRegionField,
    kQweatherJsonCodeField,
    kQweatherSuccessCode,
    kQweatherMissingCodeText,
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
    kQweatherDailyJsonDateField,
    kQweatherDailyJsonTextDayField,
    kQweatherDailyJsonIconDayField,
    kQweatherDailyJsonTempMaxField,
    kQweatherDailyJsonTempMinField,
    kQweatherDailyJsonHumidityField,
    kQweatherDailyJsonWindDirDayField,
    kQweatherDailyJsonWindScaleDayField,
    kQweatherDailyJsonSunriseField,
    kQweatherDailyJsonSunsetField,
    kQweatherAirJsonAqiField,
    kQweatherAirJsonCategoryField,
    kQweatherAirJsonPrimaryField,
    kQweatherAirJsonPm25Field,
};
#define QWEATHER_URL_INVALID_ARG_FORMAT "qweather url invalid arg stage=%s"
#define QWEATHER_URL_TOO_LONG_FORMAT "qweather %s url too long"
#define QWEATHER_RESPONSE_SIZE_INVALID_FORMAT "qweather %s response size invalid"
#define QWEATHER_RESPONSE_ALLOC_FAILED_FORMAT "qweather %s response alloc failed"
constexpr const char *kIpLocationInvalidArgLog = "ip location invalid arg";
constexpr const char *kIpLocationCoordinateTooLongLog = "ip location coordinate text too long";
constexpr const char *kIpLocationMissingCoordinateLog = "ip location response missing latitude/longitude";
#define IP_LOCATION_RESOLVED_FORMAT "ip location resolved: %s city=%s"
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
constexpr const char *kQweatherFixedWarningTexts[] = {
    kIpLocationInvalidArgLog,
    kIpLocationCoordinateTooLongLog,
    kIpLocationMissingCoordinateLog,
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
};
constexpr const char *kQweatherStageAndStatusTexts[] = {
    kQweatherDefaultStage,
    kQweatherStageIpLocation,
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

constexpr bool qweather_endpoint_and_advice_texts_nonempty()
{
    return cstr_array_nonempty(kQweatherEndpointAndAdviceTexts);
}

static_assert(array_count(kQweatherJsonFieldTexts) > 0,
              "QWeather JSON field guard must cover parsed fields");
static_assert(qweather_json_field_texts_nonempty(),
              "QWeather JSON field and business code strings must be non-empty");
static_assert(array_count(kQweatherStageAndStatusTexts) > 0,
              "QWeather stage/status guard must cover stage and status text");
static_assert(array_count(kQweatherFixedWarningTexts) > 0,
              "QWeather fixed warning guard must cover fixed warning text");
static_assert(array_count(kQweatherEndpointAndAdviceTexts) > 0,
              "QWeather endpoint/advice guard must cover endpoint and advice text");
static_assert(qweather_stage_and_status_texts_nonempty(),
              "QWeather stage, preview, status and fixed log texts must be non-empty");
static_assert(qweather_fixed_warning_texts_nonempty(),
              "QWeather fixed warning texts must be non-empty");
static_assert(qweather_endpoint_and_advice_texts_nonempty(),
              "QWeather endpoint, location, alert and advice texts must be non-empty");

struct WarningColorInfo {
    const char *code;
    const char *name;
    const char *short_name;
    int rank;
};

struct WeatherIconRange {
    int first;
    int last;
    uint32_t base_codepoint;
};

struct WeatherIconExact {
    int code;
    uint32_t codepoint;
};

constexpr WarningColorInfo kWarningColors[] = {
    {"blue", "蓝色", "蓝", 2},
    {"yellow", "黄色", "黄", 3},
    {"orange", "橙色", "橙", 4},
    {"red", "红色", "红", 5},
    {"white", "白色", "白", 1},
    {"black", "黑色", "黑", 1},
};

constexpr bool warning_color_table_valid()
{
    for (const WarningColorInfo &color : kWarningColors) {
        if (!cstr_nonempty(color.code) ||
            !cstr_nonempty(color.name) ||
            !cstr_nonempty(color.short_name) ||
            color.rank < 0) {
            return false;
        }
    }
    return true;
}

constexpr WeatherIconRange kWeatherIconRanges[] = {
    {kWeatherIconSunnyStart, kWeatherIconSunnyEnd, kWeatherIconSunnyBaseCodepoint},
    {kWeatherIconNightSunnyStart, kWeatherIconNightSunnyEnd, kWeatherIconNightSunnyBaseCodepoint},
    {kWeatherIconRainStart, kWeatherIconRainEnd, kWeatherIconRainBaseCodepoint},
    {kWeatherIconNightRainStart, kWeatherIconNightRainEnd, kWeatherIconNightRainBaseCodepoint},
    {kWeatherIconSnowStart, kWeatherIconSnowEnd, kWeatherIconSnowBaseCodepoint},
    {kWeatherIconNightSnowStart, kWeatherIconNightSnowEnd, kWeatherIconNightSnowBaseCodepoint},
    {kWeatherIconFogStart, kWeatherIconFogEnd, kWeatherIconFogBaseCodepoint},
    {kWeatherIconDustStart, kWeatherIconDustEnd, kWeatherIconDustBaseCodepoint},
    {kWeatherIconCloudStart, kWeatherIconCloudEnd, kWeatherIconCloudBaseCodepoint},
};

constexpr WeatherIconExact kWeatherIconExactCodes[] = {
    {kWeatherIconRainUnknownCode, kWeatherIconRainUnknownCodepoint},
    {kWeatherIconSnowUnknownCode, kWeatherIconSnowUnknownCodepoint},
    {kWeatherIconHotCode, kWeatherIconHotCodepoint},
    {kWeatherIconColdCode, kWeatherIconColdCodepoint},
    {kWeatherIconUnknownCode, kWeatherIconUnknownCodepoint},
};

constexpr bool weather_icon_range_table_valid()
{
    for (const WeatherIconRange &range : kWeatherIconRanges) {
        if (range.first > range.last || range.base_codepoint == 0) {
            return false;
        }
    }
    return true;
}

constexpr bool weather_icon_exact_table_valid()
{
    for (const WeatherIconExact &exact : kWeatherIconExactCodes) {
        if (exact.code < 0 || exact.codepoint == 0) {
            return false;
        }
    }
    return true;
}

static_assert(array_count(kWeatherIconRanges) > 0, "weather icon range table must not be empty");
static_assert(array_count(kWeatherIconExactCodes) > 0, "weather icon exact code table must not be empty");
static_assert(weather_icon_range_table_valid(), "weather icon range entries must be ordered and complete");
static_assert(weather_icon_exact_table_valid(), "weather icon exact entries must be complete");
static_assert(array_count(kWarningColors) > 0, "weather warning color table must not be empty");
static_assert(warning_color_table_valid(), "weather warning color entries must be complete");

void log_qweather_fixed_warning(const char *message);

const char *qweather_stage_text(const char *stage)
{
    return cstr_nonempty(stage) ? stage : kQweatherDefaultStage;
}

void copy_first_nonempty_text(char *out,
                              size_t out_len,
                              const char *first,
                              const char *second = nullptr,
                              const char *third = nullptr)
{
    if (!out || out_len == 0) {
        return;
    }
    const char *selected = cstr_nonempty(first)
                               ? first
                               : (cstr_nonempty(second) ? second : (cstr_nonempty(third) ? third : ""));
    strlcpy(out, selected, out_len);
}

bool cstr_has_suffix(const char *text, const char *suffix)
{
    if (!text || !suffix || suffix[0] == '\0') {
        return false;
    }
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

bool qweather_format_failed(int written, size_t out_len)
{
    return written < 0 || (size_t)written >= out_len;
}

void copy_ip_city_without_suffix(char *out, size_t out_len, const char *city_part)
{
    if (!out || out_len == 0) {
        return;
    }
    strlcpy(out, city_part ? city_part : "", out_len);
    if (cstr_has_suffix(out, kIpGeoCitySuffix)) {
        out[strlen(out) - strlen(kIpGeoCitySuffix)] = '\0';
    }
}

bool format_qweather_url(char *out, size_t out_len, const char *stage, const char *fmt, ...)
{
    if (!out || out_len == 0 || !fmt) {
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

bool format_ip_coordinates(char *out, size_t out_len, double longitude, double latitude)
{
    if (!out || out_len == 0) {
        log_qweather_fixed_warning(kIpLocationInvalidArgLog);
        return false;
    }
    int written = snprintf(out, out_len, kIpGeoCoordinateFormat, longitude, latitude);
    if (qweather_format_failed(written, out_len)) {
        out[0] = '\0';
        log_qweather_fixed_warning(kIpLocationCoordinateTooLongLog);
        return false;
    }
    return true;
}

void copy_ip_coordinate_location(const char *location, char *city_id, size_t city_id_len, WeatherData *weather)
{
    if (!location || !city_id || city_id_len == 0 || !weather) {
        return;
    }
    strlcpy(city_id, location, city_id_len);
    char *comma = strchr(city_id, ',');
    if (!comma) {
        return;
    }
    size_t lon_len = comma - city_id;
    if (lon_len >= sizeof(weather->lon)) {
        lon_len = sizeof(weather->lon) - 1;
    }
    memcpy(weather->lon, city_id, lon_len);
    weather->lon[lon_len] = '\0';
    strlcpy(weather->lat, comma + 1, sizeof(weather->lat));
}

void copy_ip_region_city(char *out, size_t out_len, const char *region)
{
    if (!out || out_len == 0 || !region || region[0] == '\0') {
        return;
    }

    char region_copy[kIpRegionCopySize] = {};
    strlcpy(region_copy, region, sizeof(region_copy));
    char *parts[kIpRegionMaxParts] = {};
    size_t count = 0;
    char *token = strtok(region_copy, kIpRegionDelimiter);
    while (token && count < kIpRegionMaxParts) {
        if (token[0] != '\0') {
            parts[count++] = token;
        }
        token = strtok(nullptr, kIpRegionDelimiter);
    }

    const char *city_part = count >= kIpRegionCityPartMinCount
                                ? parts[kIpRegionCityPartIndex]
                                : (count > 0 ? parts[count - 1] : "");
    copy_ip_city_without_suffix(out, out_len, city_part);
}

char *alloc_qweather_response(const char *stage, size_t buffer_size)
{
    if (buffer_size == 0) {
        ESP_LOGW(TAG, QWEATHER_RESPONSE_SIZE_INVALID_FORMAT, qweather_stage_text(stage));
        return nullptr;
    }
    char *response = (char *)malloc(buffer_size);
    if (!response) {
        ESP_LOGW(TAG, QWEATHER_RESPONSE_ALLOC_FAILED_FORMAT, qweather_stage_text(stage));
        return nullptr;
    }
    response[0] = '\0';
    return response;
}

class QweatherResponseBuffer {
public:
    QweatherResponseBuffer(const char *stage, size_t buffer_size)
        : data_(alloc_qweather_response(stage, buffer_size)),
          size_(buffer_size)
    {
    }

    ~QweatherResponseBuffer()
    {
        free(data_);
    }

    QweatherResponseBuffer(const QweatherResponseBuffer &) = delete;
    QweatherResponseBuffer &operator=(const QweatherResponseBuffer &) = delete;

    char *get() const
    {
        return data_;
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

private:
    char *data_;
    size_t size_;
};

class QweatherJsonRoot {
public:
    explicit QweatherJsonRoot(char *response)
        : root_(cJSON_Parse(response))
    {
    }

    ~QweatherJsonRoot()
    {
        cJSON_Delete(root_);
    }

    QweatherJsonRoot(const QweatherJsonRoot &) = delete;
    QweatherJsonRoot &operator=(const QweatherJsonRoot &) = delete;

    cJSON *get() const
    {
        return root_;
    }

    explicit operator bool() const
    {
        return root_ != nullptr;
    }

private:
    cJSON *root_;
};

const char *qweather_json_string_value(const cJSON *item)
{
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

bool qweather_code_ok(const cJSON *code)
{
    const char *text = qweather_json_string_value(code);
    return text && strcmp(text, kQweatherSuccessCode) == 0;
}

const char *qweather_code_text(const cJSON *code)
{
    const char *text = qweather_json_string_value(code);
    return text ? text : kQweatherMissingCodeText;
}

bool ip_geo_coordinates_available(const cJSON *lat, const cJSON *lon)
{
    return cJSON_IsNumber(lat) && cJSON_IsNumber(lon);
}

cJSON *qweather_success_item(const cJSON *root, const char *field, cJSON **code_out)
{
    cJSON *code = root ? cJSON_GetObjectItem(root, kQweatherJsonCodeField) : nullptr;
    if (code_out) {
        *code_out = code;
    }
    cJSON *item = root && field ? cJSON_GetObjectItem(root, field) : nullptr;
    return qweather_code_ok(code) ? item : nullptr;
}

cJSON *qweather_success_object(const cJSON *root, const char *field, cJSON **code_out)
{
    cJSON *item = qweather_success_item(root, field, code_out);
    return cJSON_IsObject(item) ? item : nullptr;
}

cJSON *qweather_success_array(const cJSON *root, const char *field, cJSON **code_out)
{
    cJSON *item = qweather_success_item(root, field, code_out);
    return cJSON_IsArray(item) ? item : nullptr;
}

void log_qweather_fixed_warning(const char *message)
{
    ESP_LOGW(TAG, "%s", cstr_nonempty(message) ? message : kQweatherUnknownStage);
}

const WarningColorInfo *find_warning_color(const char *code)
{
    if (!code) {
        return nullptr;
    }
    for (const WarningColorInfo &color : kWarningColors) {
        if (strcmp(code, color.code) == 0) {
            return &color;
        }
    }
    return nullptr;
}

uint32_t weather_icon_range_codepoint(int icon, int first, int last, uint32_t base_codepoint)
{
    if (icon < first || icon > last) {
        return 0;
    }
    return base_codepoint + (uint32_t)(icon - first);
}

uint32_t weather_icon_range_codepoint(int icon, const WeatherIconRange &range)
{
    return weather_icon_range_codepoint(icon, range.first, range.last, range.base_codepoint);
}

void write_weather_icon_utf8(char *out, size_t out_len, uint32_t cp)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (cp <= kUtf8OneByteMaxCodepoint) {
        if (out_len <= kUtf8OneByteLen) {
            return;
        }
        out[0] = (char)cp;
        out[1] = '\0';
        return;
    }
    if (cp <= kUtf8TwoByteMaxCodepoint) {
        if (out_len <= kUtf8TwoByteLen) {
            return;
        }
        out[0] = (char)(kUtf8TwoBytePrefix | (cp >> kUtf8Shift6));
        out[1] = (char)(kUtf8ContinuationPrefix | (cp & kUtf8ContinuationPayloadMask));
        out[2] = '\0';
        return;
    }
    if (cp <= kUtf8ThreeByteMaxCodepoint) {
        if (out_len <= kUtf8ThreeByteLen) {
            return;
        }
        out[0] = (char)(kUtf8ThreeBytePrefix | (cp >> kUtf8Shift12));
        out[1] = (char)(kUtf8ContinuationPrefix | ((cp >> kUtf8Shift6) & kUtf8ContinuationPayloadMask));
        out[2] = (char)(kUtf8ContinuationPrefix | (cp & kUtf8ContinuationPayloadMask));
        out[3] = '\0';
        return;
    }
    if (out_len <= kUtf8FourByteLen) {
        return;
    }
    out[0] = (char)(kUtf8FourBytePrefix | (cp >> kUtf8Shift18));
    out[1] = (char)(kUtf8ContinuationPrefix | ((cp >> kUtf8Shift12) & kUtf8ContinuationPayloadMask));
    out[2] = (char)(kUtf8ContinuationPrefix | ((cp >> kUtf8Shift6) & kUtf8ContinuationPayloadMask));
    out[3] = (char)(kUtf8ContinuationPrefix | (cp & kUtf8ContinuationPayloadMask));
    out[4] = '\0';
}
} // namespace

bool ip_geolocation_lookup(char *location, size_t location_len, char *city, size_t city_len)
{
    if (!location || location_len == 0 || !city || city_len == 0) {
        log_qweather_fixed_warning(kIpLocationInvalidArgLog);
        return false;
    }
    QweatherResponseBuffer response(kQweatherStageIpLocation, kIpGeoResponseBufferSize);
    if (!response) {
        return false;
    }
    if (http_get_text(kIpGeolocationUrl, response.get(), response.size()) != ESP_OK) {
        return false;
    }
    QweatherJsonRoot root(response.get());
    if (!root) {
        return false;
    }
    bool ok = false;
    cJSON *lat = cJSON_GetObjectItem(root.get(), kIpGeoJsonLatitudeField);
    cJSON *lon = cJSON_GetObjectItem(root.get(), kIpGeoJsonLongitudeField);
    cJSON *region = cJSON_GetObjectItem(root.get(), kIpGeoJsonRegionField);
    if (ip_geo_coordinates_available(lat, lon)) {
        if (!format_ip_coordinates(location, location_len, lon->valuedouble, lat->valuedouble)) {
            return false;
        }
        const char *region_text = qweather_json_string_value(region);
        if (region_text) {
            copy_ip_region_city(city, city_len, region_text);
        }
        if (city[0] == '\0') {
            strlcpy(city, location, city_len);
        }
        ESP_LOGI(TAG, IP_LOCATION_RESOLVED_FORMAT, location, city);
        ok = true;
    } else {
        log_qweather_fixed_warning(kIpLocationMissingCoordinateLog);
    }
    return ok;
}

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
    if (!location || !city_id || city_id_len == 0 || !city_name || city_name_len == 0) {
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
    cJSON *code = nullptr;
    cJSON *locations = qweather_success_array(root.get(), kQweatherJsonLocationField, &code);
    cJSON *first = cJSON_IsArray(locations) ? cJSON_GetArrayItem(locations, 0) : nullptr;
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

const char *warning_color_name(const char *code)
{
    const WarningColorInfo *color = find_warning_color(code);
    return color ? color->name : "";
}

int warning_color_rank(const char *code)
{
    const WarningColorInfo *color = find_warning_color(code);
    return color ? color->rank : 0;
}

static size_t alert_utf8_char_len(unsigned char ch)
{
    if ((ch & kUtf8AsciiMask) == 0) {
        return kUtf8OneByteLen;
    }
    if ((ch & kUtf8TwoByteMask) == kUtf8TwoBytePrefix) {
        return kUtf8TwoByteLen;
    }
    if ((ch & kUtf8ThreeByteMask) == kUtf8ThreeBytePrefix) {
        return kUtf8ThreeByteLen;
    }
    if ((ch & kUtf8FourByteMask) == kUtf8FourBytePrefix) {
        return kUtf8FourByteLen;
    }
    return kUtf8OneByteLen;
}

static size_t alert_utf8_char_count(const char *text)
{
    size_t count = 0;
    for (const unsigned char *p = (const unsigned char *)text; p && *p;) {
        size_t len = alert_utf8_char_len(*p);
        if (len == 0) {
            break;
        }
        p += len;
        ++count;
    }
    return count;
}

static void alert_utf8_copy_chars(char *out, size_t out_len, const char *in, size_t max_chars)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    size_t used = 0;
    size_t chars = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && chars < max_chars) {
        size_t len = alert_utf8_char_len(*p);
        if (used + len >= out_len) {
            break;
        }
        memcpy(out + used, p, len);
        used += len;
        p += len;
        ++chars;
    }
    out[used] = '\0';
}

static void replace_all(char *text, size_t text_len, const char *from, const char *to)
{
    if (!text || text_len == 0 || !from || !to) {
        return;
    }
    char buffer[kWeatherAlertTitleLen] = {};
    const char *read = text;
    size_t used = 0;
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    if (from_len == 0) {
        return;
    }
    while (*read && used + 1 < sizeof(buffer)) {
        if (strncmp(read, from, from_len) == 0) {
            if (used + to_len >= sizeof(buffer)) {
                break;
            }
            memcpy(buffer + used, to, to_len);
            used += to_len;
            read += from_len;
        } else {
            size_t len = alert_utf8_char_len((unsigned char)*read);
            if (used + len >= sizeof(buffer)) {
                break;
            }
            memcpy(buffer + used, read, len);
            used += len;
            read += len;
        }
    }
    buffer[used] = '\0';
    strlcpy(text, buffer, text_len);
}

static void compact_weather_alert_title(char *title, size_t title_len)
{
    if (!title || title[0] == '\0') {
        return;
    }
    if (alert_utf8_char_count(title) <= kWeatherAlertCompactTitleChars) {
        return;
    }
    replace_all(title, title_len, kWeatherAlertSuffix, "");
    for (const WarningColorInfo &color : kWarningColors) {
        replace_all(title, title_len, color.name, color.short_name);
    }
    if (alert_utf8_char_count(title) > kWeatherAlertCompactTitleChars) {
        char clipped[kWeatherAlertTitleLen] = {};
        alert_utf8_copy_chars(clipped, sizeof(clipped), title, kWeatherAlertCompactTitleChars);
        strlcpy(title, clipped, title_len);
    }
}

static void copy_compact_weather_alert_title(char *out, size_t out_len, const char *title)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!title || title[0] == '\0') {
        return;
    }
    strlcpy(out, title, out_len);
    compact_weather_alert_title(out, out_len);
}

void add_weather_alert_title(WeatherAlertData *alert, const char *title, int rank)
{
    if (!alert || !title || title[0] == '\0') {
        return;
    }

    int insert_at = alert->count;
    for (int i = 0; i < alert->count; ++i) {
        if (rank > alert->ranks[i]) {
            insert_at = i;
            break;
        }
    }

    if (alert->count < kMaxWeatherAlerts) {
        ++alert->count;
    } else if (insert_at >= kMaxWeatherAlerts) {
        return;
    }

    for (int i = alert->count - 1; i > insert_at; --i) {
        strlcpy(alert->titles[i], alert->titles[i - 1], sizeof(alert->titles[i]));
        alert->ranks[i] = alert->ranks[i - 1];
    }
    copy_compact_weather_alert_title(alert->titles[insert_at], sizeof(alert->titles[insert_at]), title);
    alert->ranks[insert_at] = rank;
    alert->active = alert->count > 0;
}

static void format_weather_alert_title_text(char *title,
                                            size_t title_len,
                                            const char *format,
                                            const char *event_name,
                                            const char *color_name = nullptr)
{
    if (!title || title_len == 0 || !format) {
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
    if (!title || title_len == 0) {
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

static void parse_weather_alert_item(cJSON *item, WeatherAlertData *alert)
{
    if (!cJSON_IsObject(item) || !alert) {
        return;
    }
    char event_name[kWeatherAlertEventNameSize] = {};
    char color_code[kWeatherAlertColorCodeSize] = {};
    char headline[kWeatherAlertHeadlineSize] = {};
    cJSON *event = cJSON_GetObjectItem(item, kQweatherAlertJsonEventTypeField);
    cJSON *color = cJSON_GetObjectItem(item, kQweatherAlertJsonColorField);
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
    cJSON *alerts = cJSON_GetObjectItem(root.get(), kQweatherAlertJsonAlertsField);
    int alert_count = cJSON_IsArray(alerts) ? cJSON_GetArraySize(alerts) : 0;
    for (int i = 0; i < alert_count; ++i) {
        cJSON *item = cJSON_GetArrayItem(alerts, i);
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

static bool parse_weather_now(cJSON *now, WeatherData *weather)
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
    cJSON *code = nullptr;
    cJSON *now = qweather_success_object(root.get(), kQweatherJsonNowField, &code);
    if (now) {
        ok = parse_weather_now(now, weather);
    } else {
        ESP_LOGW(TAG, QWEATHER_NOW_FAILED_FORMAT, qweather_code_text(code));
    }
    return ok;
}

static int weather_text_to_int(const char *text, int fallback = 0)
{
    return text && text[0] ? atoi(text) : fallback;
}

const char *weather_advice_for_day(const WeatherForecastDay &today)
{
    int temp_max = weather_text_to_int(today.temp_max);
    int temp_min = weather_text_to_int(today.temp_min, temp_max);
    const char *text = today.text;
    if (text && (strstr(text, "雨") || strstr(text, "雪"))) {
        return kWeatherAdviceRainOrSnow;
    }
    if (temp_max >= kWeatherAdviceHotTempC) {
        return kWeatherAdviceHot;
    }
    if (temp_min <= kWeatherAdviceColdTempC) {
        return kWeatherAdviceCold;
    }
    if (temp_max - temp_min >= kWeatherAdviceLargeTempGapC) {
        return kWeatherAdviceLargeTempGap;
    }
    return kWeatherAdviceCalm;
}

static void build_weather_advice(WeatherForecastData *forecast)
{
    if (!forecast || forecast->count <= 0 || !forecast->days[0].valid) {
        return;
    }
    strlcpy(forecast->advice, weather_advice_for_day(forecast->days[0]), sizeof(forecast->advice));
}

static void copy_weather_forecast_day_fields(cJSON *item, WeatherForecastDay *day)
{
    if (!item || !day) {
        return;
    }
    json_copy_string(item, kQweatherDailyJsonDateField, day->date, sizeof(day->date));
    json_copy_string(item, kQweatherDailyJsonTextDayField, day->text, sizeof(day->text));
    json_copy_string(item, kQweatherDailyJsonIconDayField, day->icon, sizeof(day->icon));
    json_copy_string(item, kQweatherDailyJsonTempMaxField, day->temp_max, sizeof(day->temp_max));
    json_copy_string(item, kQweatherDailyJsonTempMinField, day->temp_min, sizeof(day->temp_min));
    json_copy_string(item, kQweatherDailyJsonHumidityField, day->humidity, sizeof(day->humidity));
    json_copy_string(item, kQweatherDailyJsonWindDirDayField, day->wind_dir, sizeof(day->wind_dir));
    json_copy_string(item, kQweatherDailyJsonWindScaleDayField, day->wind_scale, sizeof(day->wind_scale));
    json_copy_string(item, kQweatherDailyJsonSunriseField, day->sunrise, sizeof(day->sunrise));
    json_copy_string(item, kQweatherDailyJsonSunsetField, day->sunset, sizeof(day->sunset));
}

static bool parse_weather_forecast_day(cJSON *item, WeatherForecastDay *day)
{
    if (!cJSON_IsObject(item) || !day) {
        return false;
    }
    copy_weather_forecast_day_fields(item, day);
    day->valid = day->date[0] != '\0' &&
                 (day->text[0] != '\0' || day->temp_max[0] != '\0' || day->temp_min[0] != '\0');
    return day->valid;
}

static int weather_forecast_parse_count(cJSON *daily)
{
    int count = cJSON_GetArraySize(daily);
    return count > kWeatherForecastDays ? kWeatherForecastDays : count;
}

static bool parse_weather_forecast_days(cJSON *daily, WeatherForecastData *forecast)
{
    if (!daily || !forecast) {
        return false;
    }
    int count = weather_forecast_parse_count(daily);
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(daily, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }
        WeatherForecastDay &day = forecast->days[forecast->count];
        if (parse_weather_forecast_day(item, &day)) {
            ++forecast->count;
        }
    }
    forecast->ready = forecast->count > 0;
    if (forecast->ready) {
        time(&forecast->updated_at);
        build_weather_advice(forecast);
    }
    return forecast->ready;
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
    cJSON *code = nullptr;
    cJSON *daily = qweather_success_array(root.get(), kQweatherDailyJsonDailyField, &code);
    if (daily) {
        if (parse_weather_forecast_days(daily, &next)) {
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

static bool copy_weather_air_required_fields(cJSON *now, WeatherAirData *air)
{
    return json_copy_string(now, kQweatherAirJsonAqiField, air->aqi, sizeof(air->aqi)) &&
           json_copy_string(now, kQweatherAirJsonCategoryField, air->category, sizeof(air->category));
}

static void copy_weather_air_optional_fields(cJSON *now, WeatherAirData *air)
{
    json_copy_string(now, kQweatherAirJsonPrimaryField, air->primary, sizeof(air->primary));
    json_copy_string(now, kQweatherAirJsonPm25Field, air->pm2p5, sizeof(air->pm2p5));
}

static bool parse_weather_air(cJSON *now, WeatherAirData *air)
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
    cJSON *code = nullptr;
    cJSON *now = qweather_success_object(root.get(), kQweatherJsonNowField, &code);
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

void get_weather_snapshot(WeatherData *weather, WeatherAlertData *alert)
{
    portENTER_CRITICAL(&g_weather_state_mux);
    if (weather) {
        *weather = g_weather;
    }
    if (alert) {
        *alert = g_weather_alert;
    }
    portEXIT_CRITICAL(&g_weather_state_mux);
}

void get_weather_forecast_snapshot(WeatherForecastData *forecast)
{
    if (!forecast) {
        return;
    }
    portENTER_CRITICAL(&g_weather_state_mux);
    *forecast = g_weather_forecast;
    portEXIT_CRITICAL(&g_weather_state_mux);
}

void get_weather_air_snapshot(WeatherAirData *air)
{
    if (!air) {
        return;
    }
    portENTER_CRITICAL(&g_weather_state_mux);
    *air = g_weather_air;
    portEXIT_CRITICAL(&g_weather_state_mux);
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
    xEventGroupSetBits(g_app_events, kWeatherReadyBit);
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
        xEventGroupClearBits(g_app_events, kWeatherReadyBit);
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

uint32_t weather_icon_codepoint(const char *code)
{
    if (!code || code[0] == '\0') {
        return kWeatherIconDefaultCodepoint;
    }
    int icon = atoi(code);
    for (const WeatherIconRange &range : kWeatherIconRanges) {
        uint32_t codepoint = weather_icon_range_codepoint(icon, range);
        if (codepoint != 0) {
            return codepoint;
        }
    }
    for (const WeatherIconExact &exact : kWeatherIconExactCodes) {
        if (icon == exact.code) {
            return exact.codepoint;
        }
    }
    return kWeatherIconDefaultCodepoint;
}

const char *weather_icon_text(const char *code)
{
    static char text[kWeatherIconUtf8TextSize];
    write_weather_icon_utf8(text, sizeof(text), weather_icon_codepoint(code));
    return text;
}
