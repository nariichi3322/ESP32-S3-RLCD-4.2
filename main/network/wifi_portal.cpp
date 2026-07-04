// 实现设备配网 AP、强制门户、Wi-Fi 扫描和网页保存流程。
#include "network_services.h"

#include "audio_services.h"
#include "ui_views.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <errno.h>

namespace {
TaskHandle_t s_captive_dns_task_handle = nullptr;
volatile bool s_captive_dns_stop = false;
esp_netif_t *s_ap_netif = nullptr;
constexpr size_t kCaptivePortalUriSize = 64;
char s_captive_portal_uri[kCaptivePortalUriSize] = {};
constexpr int kDnsHeaderSize = 12;
constexpr int kCaptiveDnsAnswerSize = 16;
constexpr int kCaptiveDnsPacketSize = 512;
constexpr int kDnsNamePointerSize = 2;
constexpr int kDnsTypeFieldSize = 2;
constexpr int kDnsClassFieldSize = 2;
constexpr int kDnsTtlFieldSize = 4;
constexpr int kDnsRdLengthFieldSize = 2;
constexpr int kDnsFlagsOffset = 2;
constexpr int kDnsQuestionCountOffset = 4;
constexpr int kDnsAnswerCountOffset = 6;
constexpr int kDnsAuthorityCountOffset = 8;
constexpr int kDnsAdditionalCountOffset = 10;
constexpr uint8_t kDnsLabelPointerMask = 0xC0;
constexpr uint16_t kDnsResponseFlagsStandardNoError = 0x8180;
constexpr uint16_t kDnsNameCompressionPointerToQuestion = 0xC00C;
constexpr uint16_t kDnsTypeARecord = 1;
constexpr uint16_t kDnsClassInternet = 1;
constexpr uint16_t kDnsIpv4AddressLength = 4;
constexpr uint8_t kDnsByteMask = 0xFF;
constexpr int kDnsByteShift8 = 8;
constexpr int kDnsByteShift16 = 16;
constexpr int kDnsByteShift24 = 24;
constexpr uint16_t kCaptiveDnsPort = 53;
constexpr uint32_t kCaptiveDnsTtlSeconds = 60;
constexpr int kCaptiveDnsSocketTimeoutSec = 1;
constexpr int kCaptiveDnsStopWaitAttempts = 15;
constexpr uint32_t kCaptiveDnsStopWaitDelayMs = 100;
constexpr TickType_t kCaptiveDnsStopWaitDelay = pdMS_TO_TICKS(kCaptiveDnsStopWaitDelayMs);
constexpr uint32_t kCaptiveDnsTaskStack = 3072;
constexpr UBaseType_t kCaptiveDnsTaskPriority = 3;
constexpr BaseType_t kCaptiveDnsTaskCore = 0;
constexpr const char *kCaptiveDnsTaskName = "captive_dns";
constexpr uint8_t kSetupApChannel = 1;
constexpr uint8_t kSetupApMaxConnections = 4;
constexpr uint16_t kMaxListedApCount = 32;
constexpr uint16_t kSetupHttpServerPort = 80;
constexpr size_t kSetupHttpServerStackSize = 8192;
constexpr size_t kPortalEscapedSsidSize = 80;
constexpr size_t kPortalEscapedCitySize = 80;
constexpr size_t kPortalSaveExtraTextSize = 220;
constexpr size_t kPortalRootHtmlSize = 12288;
constexpr size_t kPortalSaveResultHtmlSize = 1700;
constexpr size_t kPortalOfflineResultHtmlSize = 1200;
constexpr size_t kPortalSubmitSsidFieldSize = 33;
constexpr size_t kPortalRequestBufferSize = 640;
constexpr size_t kPortalWeatherCityIdSize = 24;
constexpr size_t kPortalWeatherCityNameSize = 32;
constexpr uint32_t kPortalSaveWifiConnectWaitMs = 12000;
constexpr uint8_t kCaptiveDnsApIpOctets[kDnsIpv4AddressLength] = {192, 168, 4, 1};
constexpr const char *kPortalSectionCloseHtml = "</div></section>";
constexpr const char *kPortalHtmlContentType = "text/html; charset=utf-8";
constexpr const char *kPortalHttpStatusInternalError = "500 Internal Server Error";
constexpr const char *kPortalHttpStatusBadRequest = "400 Bad Request";
constexpr const char *kPortalHttpStatusNoContent = "204 No Content";
constexpr const char *kPortalHttpStatusFound = "302 Found";
constexpr const char *kPortalHeaderLocation = "Location";
constexpr const char *kPortalHeaderCacheControl = "Cache-Control";
constexpr const char *kPortalCacheNoStore = "no-store";
constexpr const char *kPortalErrorNotEnoughMemory = "Not enough memory.";
constexpr const char *kPortalErrorMissingQuery = "Missing query.";
constexpr const char *kPortalWeatherCityInvalidMessage =
    "Weather city is not recognized by QWeather. Auto location has been restored.";
constexpr const char *kPortalWeatherCityDeferredMessage =
    "Weather city was saved, but online validation timed out. The next weather sync will retry.";
constexpr const char *kPortalSaveConnectedTitle = "Connected";
constexpr const char *kPortalSaveConnectingTitle = "Saved, still connecting";
constexpr const char *kPortalSaveMissingTitle = "Missing setup data";
constexpr const char *kPortalSaveConnectedBody = "The clock has joined your Wi-Fi network.";
constexpr const char *kPortalSaveConnectingBody =
    "The clock saved your settings but did not get an IP yet. Check the password or router signal, then try again.";
constexpr const char *kPortalSaveMissingBody =
    "Enter Wi-Fi and QWeather API Key, or set date and time for offline mode.";
constexpr const char *kPortalOfflineSavedTitle = "Offline mode enabled";
constexpr const char *kPortalOfflineInvalidTitle = "Invalid date or time";
constexpr const char *kPortalOfflineSavedBody = "The clock will use the RTC time and skip all network updates.";
constexpr const char *kPortalOfflineInvalidBody = "Please enter a valid date and time, or configure Wi-Fi and API Key.";
constexpr const char *kPortalWifiScanBusyMessage = "Scan busy, refresh this page.";
constexpr const char *kPortalWifiScanFailedMessage = "Scan failed, refresh this page.";
constexpr const char *kPortalWifiScanEmptyMessage = "No Wi-Fi found.";
constexpr const char *kPortalWifiScanNoMemoryMessage = "Not enough memory to list Wi-Fi.";
constexpr const char *kSetupApSsidFormat = "WeatherClock-%02X%02X";
constexpr const char *kSetupApSsidFallback = "WeatherClock-0000";
constexpr const char *kPortalFixedTexts[] = {
    kCaptiveDnsTaskName,
    kPortalSectionCloseHtml,
    kPortalHtmlContentType,
    kPortalHttpStatusInternalError,
    kPortalHttpStatusBadRequest,
    kPortalHttpStatusNoContent,
    kPortalHttpStatusFound,
    kPortalHeaderLocation,
    kPortalHeaderCacheControl,
    kPortalCacheNoStore,
    kPortalErrorNotEnoughMemory,
    kPortalErrorMissingQuery,
    kPortalWeatherCityInvalidMessage,
    kPortalWeatherCityDeferredMessage,
    kPortalSaveConnectedTitle,
    kPortalSaveConnectingTitle,
    kPortalSaveMissingTitle,
    kPortalSaveConnectedBody,
    kPortalSaveConnectingBody,
    kPortalSaveMissingBody,
    kPortalOfflineSavedTitle,
    kPortalOfflineInvalidTitle,
    kPortalOfflineSavedBody,
    kPortalOfflineInvalidBody,
    kPortalWifiScanBusyMessage,
    kPortalWifiScanFailedMessage,
    kPortalWifiScanEmptyMessage,
    kPortalWifiScanNoMemoryMessage,
    kSetupApSsidFormat,
    kSetupApSsidFallback,
};
constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr size_t cstr_len(const char *text)
{
    size_t len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *text : items) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

static_assert(kDnsHeaderSize > 0, "DNS header size must be positive");
static_assert(kCaptiveDnsAnswerSize > 0, "captive DNS answer size must be positive");
static_assert(kCaptiveDnsAnswerSize ==
                  kDnsNamePointerSize + kDnsTypeFieldSize + kDnsClassFieldSize +
                      kDnsTtlFieldSize + kDnsRdLengthFieldSize + kDnsIpv4AddressLength,
              "captive DNS answer size must match the encoded A-record layout");
static_assert(kCaptiveDnsPacketSize >= kDnsHeaderSize + kCaptiveDnsAnswerSize,
              "captive DNS packet must fit a header and answer");
static_assert(kDnsFlagsOffset + 1 < kDnsHeaderSize, "DNS flags offset must fit header");
static_assert(kDnsQuestionCountOffset + 1 < kDnsHeaderSize, "DNS question count offset must fit header");
static_assert(kDnsAnswerCountOffset + 1 < kDnsHeaderSize, "DNS answer count offset must fit header");
static_assert(kDnsAuthorityCountOffset + 1 < kDnsHeaderSize, "DNS authority count offset must fit header");
static_assert(kDnsAdditionalCountOffset + 1 < kDnsHeaderSize, "DNS additional count offset must fit header");
static_assert(kDnsIpv4AddressLength == 4, "DNS IPv4 address length must stay four bytes");
static_assert(sizeof(kCaptiveDnsApIpOctets) == kDnsIpv4AddressLength,
              "captive DNS AP IP octet table must match IPv4 length");
static_assert(kDnsByteMask == 0xFF, "DNS byte mask must keep one byte");
static_assert(kDnsByteShift8 == 8 && kDnsByteShift16 == 16 && kDnsByteShift24 == 24,
              "DNS byte shifts must match network byte order packing");
static_assert(kCaptiveDnsPort > 0, "captive DNS port must be positive");
static_assert(kCaptiveDnsTtlSeconds > 0, "captive DNS TTL must be positive");
static_assert(kCaptiveDnsSocketTimeoutSec > 0, "captive DNS socket timeout must be positive");
static_assert(kCaptiveDnsStopWaitAttempts > 0, "captive DNS stop wait attempts must be positive");
static_assert(kCaptiveDnsStopWaitDelayMs > 0, "captive DNS stop wait delay must be positive");
static_assert(kCaptiveDnsStopWaitDelay > 0, "captive DNS stop wait delay must be positive");
static_assert(kCaptiveDnsTaskStack > 0, "captive DNS task stack must be positive");
static_assert(kCaptiveDnsTaskPriority > tskIDLE_PRIORITY, "captive DNS task priority must exceed idle");
static_assert(kCaptiveDnsTaskCore >= 0, "captive DNS task core must be non-negative");
static_assert(kCaptivePortalUriSize > cstr_len(kSetupPortalUrl),
              "mutable captive portal URI must fit setup portal URL and NUL");
static_assert(kSetupApChannel > 0, "setup AP channel must be positive");
static_assert(kSetupApMaxConnections > 0, "setup AP max connections must be positive");
static_assert(kMaxListedApCount > 0, "listed AP count must be positive");
static_assert(kSetupHttpServerPort > 0, "setup HTTP server port must be positive");
static_assert(kSetupHttpServerStackSize > 0, "setup HTTP server stack must be positive");
static_assert(kPortalEscapedSsidSize > kPortalSubmitSsidFieldSize,
              "escaped SSID buffer must exceed submitted SSID field size");
static_assert(kPortalEscapedCitySize > kPortalWeatherCityNameSize,
              "escaped city buffer must exceed weather city name buffer");
static_assert(kPortalRootHtmlSize > kPortalSaveResultHtmlSize,
              "portal root HTML buffer must exceed save result buffer");
static_assert(kPortalRootHtmlSize > kPortalOfflineResultHtmlSize,
              "portal root HTML buffer must exceed offline result buffer");
static_assert(kPortalSaveExtraTextSize > 1, "portal save extra text buffer must fit text and NUL");
static_assert(kPortalRequestBufferSize > kPortalSubmitSsidFieldSize,
              "portal request buffer must exceed submitted SSID field size");
static_assert(kPortalWeatherCityIdSize > 1, "portal weather city id buffer must fit text and NUL");
static_assert(kPortalWeatherCityNameSize > 1, "portal weather city name buffer must fit text and NUL");
static_assert(kPortalSaveWifiConnectWaitMs > 0, "portal save Wi-Fi wait must be positive");
static_assert(cstr_len(kSetupApSsidFallback) < sizeof(g_ap_ssid), "setup AP SSID fallback must fit global buffer");
static_assert(cstr_nonempty(kCaptiveDnsTaskName), "captive DNS task name must be non-empty");
static_assert(cstr_nonempty(kPortalHtmlContentType), "portal HTML content type must be non-empty");
static_assert(cstr_nonempty(kSetupApSsidFormat), "setup AP SSID format must be non-empty");
static_assert(array_count(kPortalFixedTexts) > 0,
              "portal fixed text registry must not be empty");
static_assert(cstr_array_nonempty(kPortalFixedTexts), "portal fixed texts must be non-empty");
#define CAPTIVE_DNS_SOCKET_FAILED_LOG "captive dns socket failed"
#define CAPTIVE_DNS_BIND_FAILED_LOG "captive dns bind failed"
#define CAPTIVE_DNS_TIMEOUT_SETUP_FAILED_FORMAT "captive dns timeout setup failed errno=%d"
#define CAPTIVE_DNS_STARTED_LOG "captive dns started"
#define CAPTIVE_DNS_STOPPED_LOG "captive dns stopped"
#define CAPTIVE_DHCPS_STOP_FAILED_FORMAT "dhcps stop before captive setup failed: %s"
#define CAPTIVE_DHCPS_DNS_OPTION_FAILED_FORMAT "dhcps dns option failed: %s"
#define CAPTIVE_AP_DNS_SETUP_FAILED_FORMAT "ap dns setup failed: %s"
#define CAPTIVE_DHCPS_URI_OPTION_FAILED_FORMAT "dhcps captive uri option failed: %s"
#define CAPTIVE_DHCPS_RESTART_FAILED_FORMAT "dhcps restart after captive setup failed: %s"
#define CAPTIVE_DNS_TASK_STILL_STOPPING_LOG "previous captive dns task still stopping"
#define CAPTIVE_DNS_TASK_START_FAILED_LOG "captive dns task start failed"
#define SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG "setup portal running without captive dns"
#define PORTAL_HTTP_SERVER_START_FAILED_FORMAT "http server start failed: %s"
#define PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT "http uri register failed: %s"
#define PORTAL_HTML_APPEND_FAILED_LOG "setup html append failed"
#define PORTAL_HTML_TRUNCATED_FORMAT "setup html truncated buffer=%u"
#define PORTAL_POST_BODY_TRUNCATED_FORMAT "setup POST body truncated content_len=%d buffer=%u"
#define WIFI_START_SKIPPED_OFFLINE_LOG "wifi start skipped in offline mode"
#define WIFI_STA_ONLY_MODE_FAILED_FORMAT "wifi sta-only mode failed: %s"
#define WIFI_POWER_SAVE_SETUP_FAILED_FORMAT "wifi power save setup failed: %s"
#define WIFI_APSTA_MODE_FAILED_FORMAT "wifi apsta mode failed: %s"
#define WIFI_SOFTAP_CONFIG_FAILED_FORMAT "wifi softap config failed: %s"
#define WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT "wifi setup power save disable failed: %s"
#define WIFI_SETUP_AP_ACTIVE_FORMAT "setup AP active ssid=%s"
#define WIFI_SET_MODE_FAILED_FORMAT "wifi set mode failed: %s"
#define WIFI_START_FAILED_FORMAT "wifi start failed: %s"
#define WIFI_STOP_SKIPPED_OTA_LOG "wifi stop skipped during OTA"
#define WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT "wifi disconnect during stop failed: %s"
#define WIFI_STOP_FAILED_FORMAT "wifi stop failed: %s"
#define WIFI_RADIO_OFF_LOG "wifi radio off"
#define WIFI_STA_CONFIG_FAILED_FORMAT "wifi sta config failed: %s"
#define WIFI_CONNECT_START_FAILED_FORMAT "wifi connect failed to start: %s"
#define MANUAL_WEATHER_CITY_VALIDATED_FORMAT "manual weather city validated: %s id=%s"
#define MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG "manual weather city validation failed, restoring auto location"
#define MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG "manual weather city validation deferred after network/API error"
#define WIFI_DISCONNECTED_FORMAT "wifi disconnected, reason=%d"
#define WIFI_RECONNECT_START_FAILED_FORMAT "wifi reconnect failed to start: %s"
#define WIFI_GOT_IP_EVENT_MISSING_LOG "got ip event missing data"
#define WIFI_GOT_IP_FORMAT "got ip: " IPSTR
#define WIFI_STA_IP_FORMAT_FAILED_LOG "sta ip format failed"
#define WIFI_MAC_READ_FAILED_FORMAT "wifi mac read failed: %s"
#define WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG "setup AP ssid format failed"
#define WIFI_STA_NETIF_CREATE_FAILED_LOG "wifi sta netif create failed"
#define WIFI_AP_NETIF_CREATE_FAILED_LOG "wifi ap netif create failed"
#define WIFI_INIT_FAILED_FORMAT "wifi init failed: %s"
#define WIFI_STORAGE_SETUP_FAILED_FORMAT "wifi storage setup failed: %s"
#define WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT "wifi event handler register failed: %s"
#define WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT "ip event handler register failed: %s"
#define WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT "wifi initial mode setup failed: %s"
#define WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT "wifi initial softap setup failed: %s"
constexpr const char *kPortalLogTexts[] = {
    CAPTIVE_DNS_SOCKET_FAILED_LOG,
    CAPTIVE_DNS_BIND_FAILED_LOG,
    CAPTIVE_DNS_TIMEOUT_SETUP_FAILED_FORMAT,
    CAPTIVE_DNS_STARTED_LOG,
    CAPTIVE_DNS_STOPPED_LOG,
    CAPTIVE_DHCPS_STOP_FAILED_FORMAT,
    CAPTIVE_DHCPS_DNS_OPTION_FAILED_FORMAT,
    CAPTIVE_AP_DNS_SETUP_FAILED_FORMAT,
    CAPTIVE_DHCPS_URI_OPTION_FAILED_FORMAT,
    CAPTIVE_DHCPS_RESTART_FAILED_FORMAT,
    CAPTIVE_DNS_TASK_STILL_STOPPING_LOG,
    CAPTIVE_DNS_TASK_START_FAILED_LOG,
    SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG,
    PORTAL_HTTP_SERVER_START_FAILED_FORMAT,
    PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT,
    PORTAL_HTML_APPEND_FAILED_LOG,
    PORTAL_HTML_TRUNCATED_FORMAT,
    PORTAL_POST_BODY_TRUNCATED_FORMAT,
    WIFI_START_SKIPPED_OFFLINE_LOG,
    WIFI_STA_ONLY_MODE_FAILED_FORMAT,
    WIFI_POWER_SAVE_SETUP_FAILED_FORMAT,
    WIFI_APSTA_MODE_FAILED_FORMAT,
    WIFI_SOFTAP_CONFIG_FAILED_FORMAT,
    WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT,
    WIFI_SETUP_AP_ACTIVE_FORMAT,
    WIFI_SET_MODE_FAILED_FORMAT,
    WIFI_START_FAILED_FORMAT,
    WIFI_STOP_SKIPPED_OTA_LOG,
    WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT,
    WIFI_STOP_FAILED_FORMAT,
    WIFI_RADIO_OFF_LOG,
    WIFI_STA_CONFIG_FAILED_FORMAT,
    WIFI_CONNECT_START_FAILED_FORMAT,
    MANUAL_WEATHER_CITY_VALIDATED_FORMAT,
    MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG,
    MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG,
    WIFI_DISCONNECTED_FORMAT,
    WIFI_RECONNECT_START_FAILED_FORMAT,
    WIFI_GOT_IP_EVENT_MISSING_LOG,
    WIFI_GOT_IP_FORMAT,
    WIFI_STA_IP_FORMAT_FAILED_LOG,
    WIFI_MAC_READ_FAILED_FORMAT,
    WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG,
    WIFI_STA_NETIF_CREATE_FAILED_LOG,
    WIFI_AP_NETIF_CREATE_FAILED_LOG,
    WIFI_INIT_FAILED_FORMAT,
    WIFI_STORAGE_SETUP_FAILED_FORMAT,
    WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT,
    WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT,
    WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT,
    WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT,
};
static_assert(array_count(kPortalLogTexts) > 0, "portal log text registry must not be empty");
static_assert(cstr_array_nonempty(kPortalLogTexts), "portal log texts must be non-empty");
constexpr const char *kPortalHtmlHeadPrefix =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";

class WifiScanRecords {
public:
    explicit WifiScanRecords(uint16_t count)
        : records_((wifi_ap_record_t *)calloc(count, sizeof(wifi_ap_record_t))),
          capacity_(count)
    {
    }

    ~WifiScanRecords()
    {
        free(records_);
    }

    WifiScanRecords(const WifiScanRecords &) = delete;
    WifiScanRecords &operator=(const WifiScanRecords &) = delete;

    wifi_ap_record_t *data() const
    {
        return records_;
    }

    uint16_t capacity() const
    {
        return capacity_;
    }

    uint16_t count() const
    {
        return count_;
    }

    explicit operator bool() const
    {
        return records_ != nullptr;
    }

    void set_count(uint16_t count)
    {
        count_ = count <= capacity_ ? count : capacity_;
    }

private:
    wifi_ap_record_t *records_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t count_ = 0;
};

class PortalHtmlBuffer {
public:
    explicit PortalHtmlBuffer(size_t len)
        : html_((char *)calloc(1, len)),
          size_(len)
    {
    }

    ~PortalHtmlBuffer()
    {
        free(html_);
    }

    PortalHtmlBuffer(const PortalHtmlBuffer &) = delete;
    PortalHtmlBuffer &operator=(const PortalHtmlBuffer &) = delete;

    char *data() const
    {
        return html_;
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return html_ != nullptr;
    }

private:
    char *html_ = nullptr;
    size_t size_;
};

void format_sta_ip_or_clear(const esp_ip4_addr_t *ip)
{
    if (!ip) {
        g_sta_ip[0] = '\0';
        return;
    }
    int written = snprintf(g_sta_ip, sizeof(g_sta_ip), IPSTR, IP2STR(ip));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(g_sta_ip)) {
        g_sta_ip[0] = '\0';
        ESP_LOGW(TAG, WIFI_STA_IP_FORMAT_FAILED_LOG);
    }
}

void format_setup_ap_ssid(uint8_t mac4, uint8_t mac5)
{
    int written = snprintf(g_ap_ssid, sizeof(g_ap_ssid), kSetupApSsidFormat, mac4, mac5);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(g_ap_ssid)) {
        strlcpy(g_ap_ssid, kSetupApSsidFallback, sizeof(g_ap_ssid));
        ESP_LOGW(TAG, WIFI_SETUP_AP_SSID_FORMAT_FAILED_LOG);
    }
}

esp_err_t configure_softap()
{
    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, g_ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, kSetupApPassword, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(g_ap_ssid);
    ap_config.ap.channel = kSetupApChannel;
    ap_config.ap.max_connection = kSetupApMaxConnections;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

void dns_write_u16(uint8_t *buf, int offset, uint16_t value)
{
    buf[offset] = (uint8_t)(value >> kDnsByteShift8);
    buf[offset + 1] = (uint8_t)(value & kDnsByteMask);
}

void dns_write_u32(uint8_t *buf, int offset, uint32_t value)
{
    buf[offset] = (uint8_t)(value >> kDnsByteShift24);
    buf[offset + 1] = (uint8_t)((value >> kDnsByteShift16) & kDnsByteMask);
    buf[offset + 2] = (uint8_t)((value >> kDnsByteShift8) & kDnsByteMask);
    buf[offset + 3] = (uint8_t)(value & kDnsByteMask);
}

int build_captive_dns_response(const uint8_t *query, int query_len, uint8_t *response, int response_len)
{
    if (query_len < kDnsHeaderSize || response_len < query_len + kCaptiveDnsAnswerSize) {
        return 0;
    }
    uint16_t qd_count = ((uint16_t)query[kDnsQuestionCountOffset] << 8) | query[kDnsQuestionCountOffset + 1];
    if (qd_count == 0) {
        return 0;
    }

    int pos = kDnsHeaderSize;
    while (pos < query_len) {
        uint8_t label_len = query[pos++];
        if (label_len == 0) {
            break;
        }
        if ((label_len & kDnsLabelPointerMask) != 0 || pos + label_len > query_len) {
            return 0;
        }
        pos += label_len;
    }
    if (pos + 4 > query_len) {
        return 0;
    }
    int question_len = pos + 4;
    int answer_len = question_len + kCaptiveDnsAnswerSize;
    if (answer_len > response_len) {
        return 0;
    }

    memcpy(response, query, question_len);
    dns_write_u16(response, kDnsFlagsOffset, kDnsResponseFlagsStandardNoError);
    dns_write_u16(response, kDnsQuestionCountOffset, 1);
    dns_write_u16(response, kDnsAnswerCountOffset, 1);
    dns_write_u16(response, kDnsAuthorityCountOffset, 0);
    dns_write_u16(response, kDnsAdditionalCountOffset, 0);

    int out = question_len;
    dns_write_u16(response, out, kDnsNameCompressionPointerToQuestion);
    out += 2;
    dns_write_u16(response, out, kDnsTypeARecord);
    out += 2;
    dns_write_u16(response, out, kDnsClassInternet);
    out += 2;
    dns_write_u32(response, out, kCaptiveDnsTtlSeconds);
    out += 4;
    dns_write_u16(response, out, kDnsIpv4AddressLength);
    out += 2;
    for (uint8_t octet : kCaptiveDnsApIpOctets) {
        response[out++] = octet;
    }
    return out;
}

void captive_dns_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, CAPTIVE_DNS_SOCKET_FAILED_LOG);
        s_captive_dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    timeval timeout = {};
    timeout.tv_sec = kCaptiveDnsSocketTimeoutSec;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGW(TAG, CAPTIVE_DNS_TIMEOUT_SETUP_FAILED_FORMAT, errno);
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kCaptiveDnsPort);
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGW(TAG, CAPTIVE_DNS_BIND_FAILED_LOG);
        close(sock);
        s_captive_dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, CAPTIVE_DNS_STARTED_LOG);
    while (!s_captive_dns_stop) {
        uint8_t query[kCaptiveDnsPacketSize] = {};
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, query, sizeof(query), 0, (sockaddr *)&from, &from_len);
        if (len <= 0) {
            continue;
        }
        uint8_t response[kCaptiveDnsPacketSize] = {};
        int response_len = build_captive_dns_response(query, len, response, sizeof(response));
        if (response_len > 0) {
            sendto(sock, response, response_len, 0, (sockaddr *)&from, from_len);
        }
    }

    close(sock);
    s_captive_dns_task_handle = nullptr;
    ESP_LOGI(TAG, CAPTIVE_DNS_STOPPED_LOG);
    vTaskDelete(nullptr);
}

void configure_captive_portal_dhcp()
{
    if (!s_ap_netif) {
        return;
    }
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, CAPTIVE_DHCPS_STOP_FAILED_FORMAT, esp_err_to_name(err));
    }

    uint8_t offer_dns = 1;
    err = esp_netif_dhcps_option(s_ap_netif,
                                 ESP_NETIF_OP_SET,
                                 ESP_NETIF_DOMAIN_NAME_SERVER,
                                 &offer_dns,
                                 sizeof(offer_dns));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, CAPTIVE_DHCPS_DNS_OPTION_FAILED_FORMAT, esp_err_to_name(err));
    }

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ipaddr_addr(kSetupPortalIp);
    err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, CAPTIVE_AP_DNS_SETUP_FAILED_FORMAT, esp_err_to_name(err));
    }

    strlcpy(s_captive_portal_uri, kSetupPortalUrl, sizeof(s_captive_portal_uri));
    err = esp_netif_dhcps_option(s_ap_netif,
                                 ESP_NETIF_OP_SET,
                                 ESP_NETIF_CAPTIVEPORTAL_URI,
                                 s_captive_portal_uri,
                                 strlen(s_captive_portal_uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, CAPTIVE_DHCPS_URI_OPTION_FAILED_FORMAT, esp_err_to_name(err));
    }

    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(TAG, CAPTIVE_DHCPS_RESTART_FAILED_FORMAT, esp_err_to_name(err));
    }
}
} // namespace

void html_append(char *html, size_t html_len, const char *fmt, ...)
{
    if (!html || html_len == 0 || !fmt) {
        return;
    }
    size_t used = strnlen(html, html_len);
    if (used >= html_len - 1) {
        html[html_len - 1] = '\0';
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(html + used, html_len - used, fmt, args);
    va_end(args);
    if (written < 0) {
        html[used] = '\0';
        ESP_LOGW(TAG, PORTAL_HTML_APPEND_FAILED_LOG);
    } else if (written >= (int)(html_len - used)) {
        html[html_len - 1] = '\0';
        ESP_LOGW(TAG, PORTAL_HTML_TRUNCATED_FORMAT, (unsigned)html_len);
    }
}

void html_escape(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_len; ++si) {
        const char *rep = nullptr;
        if (src[si] == '&') {
            rep = "&amp;";
        } else if (src[si] == '<') {
            rep = "&lt;";
        } else if (src[si] == '>') {
            rep = "&gt;";
        } else if (src[si] == '"') {
            rep = "&quot;";
        }
        if (rep) {
            size_t rep_len = strlen(rep);
            if (di + rep_len >= dst_len) {
                break;
            }
            memcpy(dst + di, rep, rep_len);
            di += rep_len;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

esp_err_t send_portal_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, kPortalHtmlContentType);
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_portal_text_status(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    return httpd_resp_sendstr(req, text);
}

esp_err_t send_portal_empty_response(httpd_req_t *req)
{
    return httpd_resp_send(req, "", 0);
}

esp_err_t send_portal_empty_status(httpd_req_t *req, const char *status)
{
    httpd_resp_set_status(req, status);
    return send_portal_empty_response(req);
}

esp_err_t redirect_to_setup_portal(httpd_req_t *req)
{
    httpd_resp_set_status(req, kPortalHttpStatusFound);
    httpd_resp_set_hdr(req, kPortalHeaderLocation, kSetupPortalUrl);
    httpd_resp_set_hdr(req, kPortalHeaderCacheControl, kPortalCacheNoStore);
    return send_portal_empty_response(req);
}

void append_wifi_scan_message(char *html, size_t html_len, const char *message)
{
    html_append(html, html_len, "<p class='muted'>%s</p>", message ? message : "");
}

void append_wifi_scan_message_and_close(char *html, size_t html_len, const char *message)
{
    append_wifi_scan_message(html, html_len, message);
    html_append(html, html_len, kPortalSectionCloseHtml);
}

void append_wifi_scan_list(char *html, size_t html_len)
{
    if (!html || html_len == 0) {
        return;
    }
    html_append(html, html_len, "<section><div class='section-title'><span>Nearby Wi-Fi</span><a href='/'>Refresh</a></div><div class='wifi-list'>");
    wifi_scan_config_t scan_config = {};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        append_wifi_scan_message(html, html_len, kPortalWifiScanBusyMessage);
    } else {
        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanFailedMessage);
            return;
        }
        if (ap_count == 0) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanEmptyMessage);
            return;
        }
        uint16_t max_records = ap_count;
        if (max_records > kMaxListedApCount) {
            max_records = kMaxListedApCount;
        }
        WifiScanRecords records(max_records);
        if (!records) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanNoMemoryMessage);
            return;
        }
        uint16_t record_count = records.capacity();
        err = esp_wifi_scan_get_ap_records(&record_count, records.data());
        if (err != ESP_OK) {
            append_wifi_scan_message_and_close(html, html_len, kPortalWifiScanFailedMessage);
            return;
        }
        records.set_count(record_count);
        if (records.count() == 0) {
            append_wifi_scan_message(html, html_len, kPortalWifiScanEmptyMessage);
        }
        for (uint16_t i = 0; i < records.count(); ++i) {
            if (records.data()[i].ssid[0] == '\0') {
                continue;
            }
            char ssid[kPortalEscapedSsidSize] = {};
            html_escape((const char *)records.data()[i].ssid, ssid, sizeof(ssid));
            html_append(html, html_len,
                        "<button type='button' class='wifi' data-ssid=\"%s\" onclick=\"pick(this.dataset.ssid)\"><span>%s</span><b>%d dBm</b></button>",
                        ssid, ssid, records.data()[i].rssi);
        }
    }
    html_append(html, html_len, kPortalSectionCloseHtml);
}

bool apply_station_config(bool reconnect)
{
    wifi_config_t sta_config = {};
    strlcpy((char *)sta_config.sta.ssid, g_wifi_ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, g_wifi_pass, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = g_wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_STA_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    if (reconnect) {
        esp_wifi_disconnect();
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, WIFI_CONNECT_START_FAILED_FORMAT, esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

void stop_http_server()
{
    if (g_http_server) {
        httpd_stop(g_http_server);
        g_http_server = nullptr;
    }
    stop_captive_dns_server();
    g_setup_portal_active = false;
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_weather_city[kPortalEscapedCitySize] = {};
    html_escape(g_wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(g_manual_weather_city, safe_weather_city, sizeof(safe_weather_city));
    PortalHtmlBuffer html(kPortalRootHtmlSize);
    if (!html) {
        return send_portal_text_status(req, kPortalHttpStatusInternalError, kPortalErrorNotEnoughMemory);
    }
    html_append(html.data(), html.size(),
                "%s"
                "<title>WeatherClock Setup</title><style>"
                ":root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:480px;margin:0 auto;padding:22px 16px 34px}.brand{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}"
                ".mark{width:44px;height:44px;border:2px solid #17202a;border-radius:8px;display:grid;place-items:center;font-weight:900;font-size:22px;background:#fff}"
                ".pill{border:1px solid #b7c0ca;border-radius:999px;padding:7px 10px;font-size:12px;color:#465563;background:#fff}"
                "h1{font-size:26px;line-height:1.12;margin:0 0 4px}p{margin:0}.sub{font-size:14px;color:#5d6b78}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:16px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                "label{display:block;font-size:12px;font-weight:700;letter-spacing:.03em;color:#465563;margin:13px 0 6px;text-transform:uppercase}"
                "input{width:100%;height:46px;border:1px solid #aeb8c2;border-radius:6px;padding:0 12px;font-size:17px;background:#fbfcfd;color:#111;outline:none}"
                "input:focus{border-color:#17202a;box-shadow:0 0 0 3px rgba(23,32,42,.10)}.submit{width:100%;height:48px;border:0;border-radius:6px;margin-top:16px;background:#17202a;color:#fff;font-size:17px;font-weight:800}"
                "section{margin-top:16px}.section-title{display:flex;align-items:center;justify-content:space-between;margin:0 2px 8px;font-size:13px;font-weight:800;color:#465563}.section-title a{color:#17202a;text-decoration:none}"
                ".wifi-list{display:grid;gap:8px}.wifi{width:100%;border:1px solid #d3dae2;background:#fff;border-radius:6px;padding:12px;display:flex;justify-content:space-between;gap:12px;text-align:left;font-size:16px;color:#17202a}"
                ".wifi b{font-size:12px;color:#697784;white-space:nowrap}.muted{padding:12px;border:1px dashed #c7d0d9;border-radius:6px;color:#697784;background:#fbfcfd}"
                "</style><script>function pick(s){document.querySelector('[name=ssid]').value=s;document.querySelector('[name=pass]').focus();}</script></head>"
                "<body><main class='wrap'><div class='brand'><div><h1>WeatherClock</h1><p class='sub'>Connect Wi-Fi, or set time for offline mode.</p></div><div class='mark'>42</div></div>"
                "<div class='panel'><div class='pill'>Setup AP: %s</div><form method='get' action='/save' accept-charset='UTF-8'>"
                "<label>Wi-Fi SSID</label><input name='ssid' placeholder='Choose or type network name' value='%s' autocomplete='off'>"
                "<label>Password</label><input name='pass' placeholder='Wi-Fi password' type='password' autocomplete='current-password'>"
                "<label>QWeather API Key</label><input name='api_key' placeholder='Leave blank to keep saved key' value='' autocomplete='off'>"
                "<label>Weather City (Optional)</label><input name='weather_city' placeholder='Leave blank for auto location' value='%s' autocomplete='off'>"
                "<label>Offline Date & Time</label><input name='manual_time' type='datetime-local' placeholder='Set time without Wi-Fi'>"
                "<button class='submit' type='submit'>Save and connect</button></form></div>",
                kPortalHtmlHeadPrefix, g_ap_ssid, safe_ssid, safe_weather_city);
    append_wifi_scan_list(html.data(), html.size());
    html_append(html.data(), html.size(), "</main></body></html>");
    return send_portal_html(req, html.data());
}

enum ManualWeatherCityValidationResult {
    kManualWeatherCityValidationOk,
    kManualWeatherCityValidationInvalid,
    kManualWeatherCityValidationDeferred,
};

static ManualWeatherCityValidationResult validate_saved_manual_weather_city()
{
    if (!g_has_manual_weather_city || g_manual_weather_city[0] == '\0') {
        return kManualWeatherCityValidationOk;
    }
    char city_id[kPortalWeatherCityIdSize] = {};
    char city_name[kPortalWeatherCityNameSize] = {};
    QweatherCityLookupStatus status = qweather_lookup_city_status(g_manual_weather_city,
                                                                  city_id,
                                                                  sizeof(city_id),
                                                                  city_name,
                                                                  sizeof(city_name));
    if (status == kQweatherCityLookupOk) {
        ESP_LOGI(TAG, MANUAL_WEATHER_CITY_VALIDATED_FORMAT, city_name, city_id);
        return kManualWeatherCityValidationOk;
    }
    if (status == kQweatherCityLookupNotFound) {
        ESP_LOGW(TAG, MANUAL_WEATHER_CITY_VALIDATION_FAILED_LOG);
        (void)clear_manual_weather_city();
        return kManualWeatherCityValidationInvalid;
    }
    ESP_LOGW(TAG, MANUAL_WEATHER_CITY_VALIDATION_DEFERRED_LOG);
    return kManualWeatherCityValidationDeferred;
}

esp_err_t send_save_result_page(httpd_req_t *req, bool saved, bool connected, const char *extra_message)
{
    char safe_ssid[kPortalEscapedSsidSize] = {};
    char safe_city[kPortalEscapedCitySize] = {};
    char safe_extra[kPortalSaveExtraTextSize] = {};
    html_escape(g_wifi_ssid, safe_ssid, sizeof(safe_ssid));
    html_escape(g_has_manual_weather_city ? g_manual_weather_city : "Auto", safe_city, sizeof(safe_city));
    html_escape(extra_message ? extra_message : "", safe_extra, sizeof(safe_extra));
    char html[kPortalSaveResultHtmlSize] = {};
    const char *title = saved ? (connected ? kPortalSaveConnectedTitle : kPortalSaveConnectingTitle)
                              : kPortalSaveMissingTitle;
    const char *body = saved ? (connected ? kPortalSaveConnectedBody : kPortalSaveConnectingBody)
                             : kPortalSaveMissingBody;
    html_append(html, sizeof(html),
                "%s"
                "<title>WeatherClock Setup</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:48px;height:48px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:24px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}.note{border:1px solid #d3dae2;border-radius:6px;padding:10px;margin:0 0 14px;color:#17202a;background:#fbfcfd;font-size:14px}.meta{border-top:1px solid #e1e6eb;padding-top:12px;color:#697784;font-size:13px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style></head><body><main class='wrap'><section class='panel'><div class='state'>%s</div><h1>%s</h1><p>%s</p>"
                "%s%s%s<div class='meta'>SSID: %s<br>Weather city: %s<br>Last Wi-Fi reason: %d</div><a href='/'>Back to setup</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                connected ? "OK" : "!",
                title,
                body,
                safe_extra[0] ? "<div class='note'>" : "",
                safe_extra,
                safe_extra[0] ? "</div>" : "",
                safe_ssid,
                safe_city,
                g_last_wifi_disconnect_reason);
    return send_portal_html(req, html);
}

esp_err_t send_offline_result_page(httpd_req_t *req, bool saved)
{
    char html[kPortalOfflineResultHtmlSize] = {};
    html_append(html, sizeof(html),
                "%s"
                "<title>WeatherClock Offline</title><style>"
                "*{box-sizing:border-box}body{margin:0;background:#eef1f5;color:#17202a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".wrap{max-width:460px;margin:0 auto;padding:28px 16px}.panel{background:#fff;border:1px solid #d3dae2;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(23,32,42,.08)}"
                ".state{width:48px;height:48px;border-radius:8px;border:2px solid #17202a;display:grid;place-items:center;font-size:24px;font-weight:900;margin-bottom:14px}"
                "h1{font-size:24px;margin:0 0 8px}p{font-size:15px;line-height:1.45;color:#4d5b68;margin:0 0 14px}"
                "a{display:block;height:46px;line-height:46px;text-align:center;background:#17202a;color:#fff;text-decoration:none;border-radius:6px;font-weight:800;margin-top:16px}"
                "</style></head><body><main class='wrap'><section class='panel'><div class='state'>%s</div><h1>%s</h1><p>%s</p><a href='/'>Back to setup</a></section></main></body></html>",
                kPortalHtmlHeadPrefix,
                saved ? "OK" : "!",
                saved ? kPortalOfflineSavedTitle : kPortalOfflineInvalidTitle,
                saved ? kPortalOfflineSavedBody : kPortalOfflineInvalidBody);
    return send_portal_html(req, html);
}

static esp_err_t handle_setup_save(httpd_req_t *req, const char *body)
{
    char ssid[kPortalSubmitSsidFieldSize] = {};
    form_value(body, "ssid", ssid, sizeof(ssid));
    trim_ascii(ssid);
    if (ssid[0] == '\0') {
        bool offline_saved = save_offline_datetime_from_body(body);
        esp_err_t err = send_offline_result_page(req, offline_saved);
        if (offline_saved) {
            g_settings_requested = false;
            g_network_diag_page_requested = false;
            g_boot_info_requested = false;
            stop_wifi_radio(true);
            notify_ui_task();
        }
        return err;
    }
    bool saved = save_credentials_from_body(body);
    bool connected = saved && wait_for_wifi_connected(kPortalSaveWifiConnectWaitMs);
    const char *extra_message = nullptr;
    if (connected && g_has_manual_weather_city) {
        ManualWeatherCityValidationResult city_result = validate_saved_manual_weather_city();
        if (city_result == kManualWeatherCityValidationInvalid) {
            extra_message = kPortalWeatherCityInvalidMessage;
        } else if (city_result == kManualWeatherCityValidationDeferred) {
            extra_message = kPortalWeatherCityDeferredMessage;
        }
    }
    esp_err_t err = send_save_result_page(req, saved, connected, extra_message);
    if (connected) {
        xEventGroupSetBits(g_app_events, kProvisioningSyncBit);
    }
    return err;
}

esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[kPortalRequestBufferSize] = {};
    int total = 0;
    while (total < req->content_len && total < (int)sizeof(body) - 1) {
        int ret = httpd_req_recv(req, body + total, sizeof(body) - 1 - total);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        total += ret;
    }
    body[total] = '\0';
    if (total < req->content_len) {
        ESP_LOGW(TAG, PORTAL_POST_BODY_TRUNCATED_FORMAT, req->content_len, (unsigned)sizeof(body));
    }

    return handle_setup_save(req, body);
}

esp_err_t save_get_handler(httpd_req_t *req)
{
    char query[kPortalRequestBufferSize] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_portal_text_status(req, kPortalHttpStatusBadRequest, kPortalErrorMissingQuery);
    }
    return handle_setup_save(req, query);
}

esp_err_t empty_asset_handler(httpd_req_t *req)
{
    return send_portal_empty_status(req, kPortalHttpStatusNoContent);
}

esp_err_t captive_portal_handler(httpd_req_t *req)
{
    return redirect_to_setup_portal(req);
}

esp_err_t register_http_handler(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    return httpd_register_uri_handler(server, &route);
}

esp_err_t register_http_handler_if_ok(esp_err_t previous,
                                      httpd_handle_t server,
                                      const char *uri,
                                      httpd_method_t method,
                                      esp_err_t (*handler)(httpd_req_t *))
{
    return previous == ESP_OK ? register_http_handler(server, uri, method, handler) : previous;
}

bool start_captive_dns_server()
{
    if (s_captive_dns_task_handle) {
        if (!s_captive_dns_stop) {
            return true;
        }
        for (int i = 0; i < kCaptiveDnsStopWaitAttempts && s_captive_dns_task_handle; ++i) {
            vTaskDelay(kCaptiveDnsStopWaitDelay);
        }
        if (s_captive_dns_task_handle) {
            ESP_LOGW(TAG, CAPTIVE_DNS_TASK_STILL_STOPPING_LOG);
            return false;
        }
    }
    s_captive_dns_stop = false;
    BaseType_t ok = xTaskCreatePinnedToCore(captive_dns_task,
                                            kCaptiveDnsTaskName,
                                            kCaptiveDnsTaskStack,
                                            nullptr,
                                            kCaptiveDnsTaskPriority,
                                            &s_captive_dns_task_handle,
                                            kCaptiveDnsTaskCore);
    if (ok != pdPASS) {
        s_captive_dns_task_handle = nullptr;
        ESP_LOGW(TAG, CAPTIVE_DNS_TASK_START_FAILED_LOG);
        return false;
    }
    return true;
}

void stop_captive_dns_server()
{
    if (!s_captive_dns_task_handle) {
        s_captive_dns_stop = false;
        return;
    }
    s_captive_dns_stop = true;
}

bool start_http_server()
{
    if (g_http_server) {
        g_setup_portal_active = true;
        if (!start_captive_dns_server()) {
            ESP_LOGW(TAG, SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG);
        }
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kSetupHttpServerPort;
    config.stack_size = kSetupHttpServerStackSize;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&g_http_server, &config);
    if (err != ESP_OK) {
        g_http_server = nullptr;
        g_setup_portal_active = false;
        ESP_LOGW(TAG, PORTAL_HTTP_SERVER_START_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }

    err = register_http_handler_if_ok(err, g_http_server, "/", HTTP_GET, root_get_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/save", HTTP_POST, save_post_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/save", HTTP_GET, save_get_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/favicon.ico", HTTP_GET, empty_asset_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/apple-touch-icon.png", HTTP_GET, empty_asset_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/apple-touch-icon-precomposed.png", HTTP_GET, empty_asset_handler);
    err = register_http_handler_if_ok(err, g_http_server, "/*", HTTP_GET, captive_portal_handler);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, PORTAL_HTTP_URI_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        httpd_stop(g_http_server);
        g_http_server = nullptr;
        g_setup_portal_active = false;
        return false;
    }
    if (!start_captive_dns_server()) {
        ESP_LOGW(TAG, SETUP_PORTAL_WITHOUT_CAPTIVE_DNS_LOG);
    }
    g_setup_portal_active = true;
    return true;
}

bool start_wifi_radio(bool enable_setup_portal)
{
    if (g_offline_mode_ui_enabled && !enable_setup_portal) {
        ESP_LOGI(TAG, WIFI_START_SKIPPED_OFFLINE_LOG);
        return false;
    }
    bool entering_setup_portal = enable_setup_portal && !g_setup_portal_active;
    if (g_wifi_radio_on) {
        if (!enable_setup_portal) {
            stop_http_server();
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_STA_ONLY_MODE_FAILED_FORMAT, esp_err_to_name(mode_err));
                return false;
            }
            esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
            if (ps_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_POWER_SAVE_SETUP_FAILED_FORMAT, esp_err_to_name(ps_err));
            }
        } else {
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_APSTA_MODE_FAILED_FORMAT, esp_err_to_name(mode_err));
                return false;
            }
            esp_err_t ap_err = configure_softap();
            if (ap_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_SOFTAP_CONFIG_FAILED_FORMAT, esp_err_to_name(ap_err));
                return false;
            }
            esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_err != ESP_OK) {
                ESP_LOGW(TAG, WIFI_SETUP_POWER_SAVE_DISABLE_FAILED_FORMAT, esp_err_to_name(ps_err));
            }
            if (!g_have_wifi_creds) {
                (void)esp_wifi_disconnect();
                xEventGroupClearBits(g_app_events, kWifiConnectedBit);
                g_sta_ip[0] = '\0';
            }
        }
        if (enable_setup_portal && !g_setup_portal_active) {
            if (!start_http_server()) {
                return false;
            }
            ESP_LOGI(TAG, WIFI_SETUP_AP_ACTIVE_FORMAT, g_ap_ssid);
        }
        if (g_have_wifi_creds) {
            (void)apply_station_config(true);
        }
        if (entering_setup_portal) {
            request_setup_prompt_once();
        }
        return true;
    }

    g_wifi_stop_requested = false;
    esp_err_t err = esp_wifi_set_mode(enable_setup_portal ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_SET_MODE_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    if (enable_setup_portal) {
        err = configure_softap();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, WIFI_SOFTAP_CONFIG_FAILED_FORMAT, esp_err_to_name(err));
            return false;
        }
    }
    if (g_have_wifi_creds) {
        (void)apply_station_config(false);
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_START_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    esp_err_t ps_err = esp_wifi_set_ps(enable_setup_portal ? WIFI_PS_NONE : WIFI_PS_MAX_MODEM);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_POWER_SAVE_SETUP_FAILED_FORMAT, esp_err_to_name(ps_err));
    }
    if (enable_setup_portal) {
        if (!start_http_server()) {
            g_wifi_stop_requested = true;
            (void)esp_wifi_disconnect();
            (void)esp_wifi_stop();
            return false;
        }
        if (entering_setup_portal) {
            request_setup_prompt_once();
        }
        ESP_LOGI(TAG, WIFI_SETUP_AP_ACTIVE_FORMAT, g_ap_ssid);
    }
    g_wifi_radio_on = true;
    return true;
}

void stop_wifi_radio(bool force_setup_portal)
{
    if (!g_wifi_radio_on) {
        return;
    }
    if ((g_ota_state == kOtaChecking || g_ota_state == kOtaUpdating) && !force_setup_portal) {
        ESP_LOGI(TAG, WIFI_STOP_SKIPPED_OTA_LOG);
        return;
    }
    if (g_setup_portal_active && !force_setup_portal) {
        return;
    }
    if (!g_have_wifi_creds && !force_setup_portal) {
        return;
    }
    stop_http_server();
    g_wifi_stop_requested = true;
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, WIFI_DISCONNECT_DURING_STOP_FAILED_FORMAT, esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, WIFI_STOP_FAILED_FORMAT, esp_err_to_name(err));
        g_wifi_stop_requested = false;
    } else {
        g_wifi_radio_on = false;
        g_wifi_stop_requested = false;
        xEventGroupClearBits(g_app_events, kWifiConnectedBit);
        g_sta_ip[0] = '\0';
        ESP_LOGI(TAG, WIFI_RADIO_OFF_LOG);
    }
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && g_have_wifi_creds) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        g_last_wifi_disconnect_reason = event ? event->reason : -1;
        g_sta_ip[0] = '\0';
        ESP_LOGW(TAG, WIFI_DISCONNECTED_FORMAT, event ? event->reason : -1);
        xEventGroupClearBits(g_app_events, kWifiConnectedBit);
        notify_ui_task();
        if (g_have_wifi_creds && g_wifi_radio_on && !g_wifi_stop_requested) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, WIFI_RECONNECT_START_FAILED_FORMAT, esp_err_to_name(err));
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (!event) {
            ESP_LOGW(TAG, WIFI_GOT_IP_EVENT_MISSING_LOG);
            return;
        }
        ESP_LOGI(TAG, WIFI_GOT_IP_FORMAT, IP2STR(&event->ip_info.ip));
        format_sta_ip_or_clear(&event->ip_info.ip);
        xEventGroupSetBits(g_app_events, kWifiConnectedBit);
        notify_ui_task();
    }
}

void init_wifi()
{
    uint8_t mac[6] = {};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_MAC_READ_FAILED_FORMAT, esp_err_to_name(err));
    }
    format_setup_ap_ssid(mac[4], mac[5]);

    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGW(TAG, WIFI_STA_NETIF_CREATE_FAILED_LOG);
        return;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGW(TAG, WIFI_AP_NETIF_CREATE_FAILED_LOG);
        return;
    }
    configure_captive_portal_dhcp();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INIT_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_STORAGE_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_EVENT_HANDLER_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_IP_EVENT_HANDLER_REGISTER_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INITIAL_MODE_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }
    err = configure_softap();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, WIFI_INITIAL_SOFTAP_SETUP_FAILED_FORMAT, esp_err_to_name(err));
        return;
    }

    if (!g_have_wifi_creds && !g_offline_mode_ui_enabled) {
        start_wifi_radio(true);
    }
}
