// 执行设置页里的网络诊断流程，逐项显示联网链路状态。
#include "network_services.h"

#include "ui_views.h"

#include "lwip/netdb.h"

namespace {
constexpr size_t kNetworkDiagPublicIpResponseBufferSize = 2048;
constexpr size_t kNetworkDiagDefaultProbeBufferSize = 512;
constexpr size_t kNetworkDiagWideProbeBufferSize = 1024;
constexpr size_t kNetworkDiagLocationTextSize = 32;
constexpr size_t kNetworkDiagCityTextSize = 32;
constexpr size_t kNetworkDiagPublicIpTextSize = 48;
constexpr int kNetworkDiagJsonSearchMaxDepth = 8;
constexpr int kNetworkDiagNtpMaxRetries = 5;
constexpr const char *kNetworkDiagPublicIpUrl = "https://uapis.cn/api/v1/network/myip";
constexpr const char *kNetworkDiagPublicIpJsonKey = "ip";
constexpr const char *kNetworkDiagQweatherDnsHost = "dev.qweather.com";
constexpr const char *kNetworkDiagGithubDnsHost = "raw.githubusercontent.com";
constexpr const char *kNetworkDiagStatusWaiting = "等待";
constexpr const char *kNetworkDiagStatusChecking = "检测中";
constexpr const char *kNetworkDiagStatusFailed = "超时/失败";
constexpr const char *kNetworkDiagStatusOk = "OK";
constexpr const char *kNetworkDiagPlaceholder = "--";
constexpr const char *kNetworkDiagLocalIpFormat = "本地IP: %s";
constexpr const char *kNetworkDiagPublicIpFormat = "公网IP: %s";
constexpr const char *kNetworkDiagIpLocationFormat = "IP定位: %s";
constexpr const char *kNetworkDiagIpLocationCityFormat = "IP定位: %s %s";
constexpr const char *kNetworkDiagDnsFormat = "DNS: %s";
constexpr const char *kNetworkDiagWeatherFormat = "天气: %s";
constexpr const char *kNetworkDiagNtpFormat = "NTP: %s";
constexpr const char *kNetworkDiagSayingFormat = "一言: %s";
constexpr const char *kNetworkDiagInternetFormat = "公网: %s";
constexpr const char *kNetworkDiagOtaFormat = "OTA源: %s";
constexpr size_t kNetworkDiagIpv4TextMinSize = sizeof("255.255.255.255");
#define NETWORK_DIAG_RESPONSE_ALLOC_FAILED_FORMAT "network diag response alloc failed len=%u"
#define NETWORK_DIAG_DNS_INVALID_HOST_LOG "network diag dns invalid host"
#define NETWORK_DIAG_DNS_LOOKUP_FAILED_FORMAT "network diag dns lookup failed host=%s rc=%d"
#define NETWORK_DIAG_HTTP_PROBE_INVALID_ARG_LOG "network diag http probe invalid arg"
#define NETWORK_DIAG_PUBLIC_IP_PARSE_FAILED_LOG "network diag public ip parse failed"
#define NETWORK_DIAG_PUBLIC_IP_HTTP_FAILED_LOG "network diag public ip http failed"
#define NETWORK_DIAG_LINE_INDEX_INVALID_FORMAT "network diag line index invalid: %d"
#define NETWORK_DIAG_LINE_FORMAT_FAILED_FORMAT "network diag line format failed index=%d"
#define NETWORK_DIAG_LINE_TRUNCATED_FORMAT "network diag line truncated index=%d len=%d"
constexpr size_t kNetworkDiagTextCount = 28;
constexpr const char *const kNetworkDiagTexts[] = {
    kNetworkDiagPublicIpUrl,
    kNetworkDiagPublicIpJsonKey,
    kNetworkDiagQweatherDnsHost,
    kNetworkDiagGithubDnsHost,
    kNetworkDiagStatusWaiting,
    kNetworkDiagStatusChecking,
    kNetworkDiagStatusFailed,
    kNetworkDiagStatusOk,
    kNetworkDiagPlaceholder,
    kNetworkDiagLocalIpFormat,
    kNetworkDiagPublicIpFormat,
    kNetworkDiagIpLocationFormat,
    kNetworkDiagIpLocationCityFormat,
    kNetworkDiagDnsFormat,
    kNetworkDiagWeatherFormat,
    kNetworkDiagNtpFormat,
    kNetworkDiagSayingFormat,
    kNetworkDiagInternetFormat,
    kNetworkDiagOtaFormat,
    NETWORK_DIAG_RESPONSE_ALLOC_FAILED_FORMAT,
    NETWORK_DIAG_DNS_INVALID_HOST_LOG,
    NETWORK_DIAG_DNS_LOOKUP_FAILED_FORMAT,
    NETWORK_DIAG_HTTP_PROBE_INVALID_ARG_LOG,
    NETWORK_DIAG_PUBLIC_IP_PARSE_FAILED_LOG,
    NETWORK_DIAG_PUBLIC_IP_HTTP_FAILED_LOG,
    NETWORK_DIAG_LINE_INDEX_INVALID_FORMAT,
    NETWORK_DIAG_LINE_FORMAT_FAILED_FORMAT,
    NETWORK_DIAG_LINE_TRUNCATED_FORMAT,
};
constexpr int kNetworkDiagLocalIpLine = 0;
constexpr int kNetworkDiagPublicIpLine = 1;
constexpr int kNetworkDiagIpLocationLine = 2;
constexpr int kNetworkDiagDnsLine = 3;
constexpr int kNetworkDiagWeatherLine = 4;
constexpr int kNetworkDiagNtpLine = 5;
constexpr int kNetworkDiagSayingLine = 6;
constexpr int kNetworkDiagInternetLine = 7;
constexpr int kNetworkDiagOtaLine = 8;

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
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

static_assert(kNetworkDiagDefaultProbeBufferSize > 0, "network diag default probe buffer must be nonzero");
static_assert(kNetworkDiagWideProbeBufferSize >= kNetworkDiagDefaultProbeBufferSize,
              "network diag wide probe buffer must cover default probe buffer");
static_assert(kNetworkDiagPublicIpResponseBufferSize >= kNetworkDiagWideProbeBufferSize,
              "public IP response buffer must cover wide probe responses");
static_assert(kNetworkDiagLocationTextSize > 1, "network diag location text buffer must fit text and NUL");
static_assert(kNetworkDiagCityTextSize > 1, "network diag city text buffer must fit text and NUL");
static_assert(kNetworkDiagPublicIpTextSize > 1, "network diag public IP text buffer must fit text and NUL");
static_assert(kNetworkDiagPublicIpTextSize >= kNetworkDiagIpv4TextMinSize,
              "network diag public IP text buffer must fit IPv4 text");
static_assert(kNetworkDiagJsonSearchMaxDepth >= 0, "network diag JSON search depth must be non-negative");
static_assert(kNetworkDiagNtpMaxRetries > 0, "network diag NTP retry count must be positive");
static_assert(array_count(kNetworkDiagTexts) == kNetworkDiagTextCount,
              "network diagnostic text guard must cover every fixed text and log");
static_assert(cstr_array_nonempty(kNetworkDiagTexts), "network diagnostic fixed texts and logs must be non-empty");
static_assert(kNetworkDiagLocalIpLine >= 0 && kNetworkDiagLocalIpLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagPublicIpLine >= 0 && kNetworkDiagPublicIpLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagIpLocationLine >= 0 && kNetworkDiagIpLocationLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagDnsLine >= 0 && kNetworkDiagDnsLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagWeatherLine >= 0 && kNetworkDiagWeatherLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagNtpLine >= 0 && kNetworkDiagNtpLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagSayingLine >= 0 && kNetworkDiagSayingLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagInternetLine >= 0 && kNetworkDiagInternetLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagOtaLine >= 0 && kNetworkDiagOtaLine < kNetworkDiagLineCount);
static_assert(kNetworkDiagOtaLine == kNetworkDiagLineCount - 1,
              "network diag OTA line must remain the final diagnostic row");
static_assert(kNetworkDiagLocalIpLine < kNetworkDiagPublicIpLine &&
                  kNetworkDiagPublicIpLine < kNetworkDiagIpLocationLine &&
                  kNetworkDiagIpLocationLine < kNetworkDiagDnsLine &&
                  kNetworkDiagDnsLine < kNetworkDiagWeatherLine &&
                  kNetworkDiagWeatherLine < kNetworkDiagNtpLine &&
                  kNetworkDiagNtpLine < kNetworkDiagSayingLine &&
                  kNetworkDiagSayingLine < kNetworkDiagInternetLine &&
                  kNetworkDiagInternetLine < kNetworkDiagOtaLine,
              "network diag line order must match UI initialization and execution order");

class NetworkDiagResponseBuffer {
public:
    explicit NetworkDiagResponseBuffer(size_t buffer_len)
        : data_((char *)calloc(buffer_len, 1)),
          size_(buffer_len)
    {
        if (!data_) {
            ESP_LOGW(TAG, NETWORK_DIAG_RESPONSE_ALLOC_FAILED_FORMAT, (unsigned)buffer_len);
        }
    }

    ~NetworkDiagResponseBuffer()
    {
        free(data_);
    }

    NetworkDiagResponseBuffer(const NetworkDiagResponseBuffer &) = delete;
    NetworkDiagResponseBuffer &operator=(const NetworkDiagResponseBuffer &) = delete;

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

class NetworkDiagJsonRoot {
public:
    explicit NetworkDiagJsonRoot(char *response)
        : root_(cJSON_Parse(response))
    {
    }

    ~NetworkDiagJsonRoot()
    {
        cJSON_Delete(root_);
    }

    NetworkDiagJsonRoot(const NetworkDiagJsonRoot &) = delete;
    NetworkDiagJsonRoot &operator=(const NetworkDiagJsonRoot &) = delete;

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

void diag_count(bool ok)
{
    g_network_diag_total = g_network_diag_total + 1;
    if (ok) {
        g_network_diag_passed = g_network_diag_passed + 1;
    }
}

const char *diag_result_text(bool ok)
{
    return ok ? kNetworkDiagStatusOk : kNetworkDiagStatusFailed;
}

bool dns_lookup_ok(const char *host)
{
    if (!host || host[0] == '\0') {
        ESP_LOGW(TAG, "%s", NETWORK_DIAG_DNS_INVALID_HOST_LOG);
        return false;
    }
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    int rc = getaddrinfo(host, nullptr, &hints, &result);
    if (result) {
        freeaddrinfo(result);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, NETWORK_DIAG_DNS_LOOKUP_FAILED_FORMAT, host, rc);
    }
    return rc == 0;
}

bool http_probe_ok(const char *url, size_t buffer_len = kNetworkDiagDefaultProbeBufferSize)
{
    if (!url || url[0] == '\0' || buffer_len == 0) {
        ESP_LOGW(TAG, "%s", NETWORK_DIAG_HTTP_PROBE_INVALID_ARG_LOG);
        return false;
    }
    NetworkDiagResponseBuffer response(buffer_len);
    if (!response) {
        return false;
    }
    return http_get_text(url, response.get(), response.size(), nullptr) == ESP_OK;
}

bool find_json_string_recursive(cJSON *node, const char *name, char *out, size_t out_len, int depth = 0)
{
    if (!node || !name || !out || out_len == 0) {
        return false;
    }
    if (depth > kNetworkDiagJsonSearchMaxDepth) {
        return false;
    }
    if (cJSON_IsObject(node)) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
        if (cJSON_IsString(item) && item->valuestring) {
            strlcpy(out, item->valuestring, out_len);
            return true;
        }
        cJSON_ArrayForEach(item, node)
        {
            if (find_json_string_recursive(item, name, out, out_len, depth + 1)) {
                return true;
            }
        }
    } else if (cJSON_IsArray(node)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, node)
        {
            if (find_json_string_recursive(item, name, out, out_len, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}

bool network_diag_token_space(char ch)
{
    return ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t';
}

bool copy_first_token_if_ip_like(const char *text, char *out, size_t out_len)
{
    if (!text || !out || out_len == 0) {
        return false;
    }
    const char *start = text;
    while (network_diag_token_space(*start)) {
        ++start;
    }
    if (!*start || !strchr(start, '.')) {
        return false;
    }
    size_t len = 0;
    while (start[len] && !network_diag_token_space(start[len])) {
        ++len;
    }
    if (len == 0 || len >= out_len) {
        return false;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool lookup_public_ip(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    NetworkDiagResponseBuffer response(kNetworkDiagPublicIpResponseBufferSize);
    if (!response) {
        return false;
    }
    bool ok = false;
    if (http_get_text(kNetworkDiagPublicIpUrl,
                      response.get(),
                      response.size(),
                      nullptr) == ESP_OK) {
        NetworkDiagJsonRoot root(response.get());
        if (root) {
            ok = find_json_string_recursive(root.get(), kNetworkDiagPublicIpJsonKey, out, out_len);
        }
        if (!ok) {
            ok = copy_first_token_if_ip_like(response.get(), out, out_len);
        }
        if (!ok) {
            ESP_LOGW(TAG, "%s", NETWORK_DIAG_PUBLIC_IP_PARSE_FAILED_LOG);
        }
    } else {
        ESP_LOGW(TAG, "%s", NETWORK_DIAG_PUBLIC_IP_HTTP_FAILED_LOG);
    }
    return ok;
}
} // namespace

void network_diag_reset()
{
    g_network_diag_state = kNetworkDiagIdle;
    g_network_diag_step = 0;
    g_network_diag_passed = 0;
    g_network_diag_total = 0;
    for (int i = 0; i < kNetworkDiagLineCount; ++i) {
        g_network_diag_lines[i][0] = '\0';
    }
}

void network_diag_begin()
{
    g_network_diag_state = kNetworkDiagRunning;
    g_network_diag_step = 0;
    g_network_diag_passed = 0;
    g_network_diag_total = 0;
    network_diag_set_line(kNetworkDiagLocalIpLine, kNetworkDiagLocalIpFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagPublicIpLine, kNetworkDiagPublicIpFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagIpLocationLine, kNetworkDiagIpLocationFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagDnsLine, kNetworkDiagDnsFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagWeatherLine, kNetworkDiagWeatherFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagNtpLine, kNetworkDiagNtpFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagSayingLine, kNetworkDiagSayingFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagInternetLine, kNetworkDiagInternetFormat, kNetworkDiagStatusWaiting);
    network_diag_set_line(kNetworkDiagOtaLine, kNetworkDiagOtaFormat, kNetworkDiagStatusWaiting);
}

void network_diag_finish()
{
    g_network_diag_state = kNetworkDiagDone;
    g_network_diag_step = kNetworkDiagLineCount;
    notify_ui_task();
}

void network_diag_set_line(int index, const char *fmt, ...)
{
    if (index < 0 || index >= kNetworkDiagLineCount) {
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_INDEX_INVALID_FORMAT, index);
        return;
    }
    if (!fmt) {
        g_network_diag_lines[index][0] = '\0';
        notify_ui_task();
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(g_network_diag_lines[index], kNetworkDiagLineLen, fmt, args);
    va_end(args);
    if (written < 0) {
        g_network_diag_lines[index][0] = '\0';
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_FORMAT_FAILED_FORMAT, index);
    } else if (written >= kNetworkDiagLineLen) {
        g_network_diag_lines[index][kNetworkDiagLineLen - 1] = '\0';
        ESP_LOGW(TAG, NETWORK_DIAG_LINE_TRUNCATED_FORMAT, index, written);
    }
    notify_ui_task();
}

namespace {
void network_diag_set_result_line(int index, const char *fmt, bool ok)
{
    network_diag_set_line(index, fmt, diag_result_text(ok));
}
} // namespace

void run_network_diagnostics()
{
    network_diag_begin();

    char location[kNetworkDiagLocationTextSize] = {};
    char city[kNetworkDiagCityTextSize] = {};
    char public_ip[kNetworkDiagPublicIpTextSize] = {};
    bool local_ip_ok = g_sta_ip[0] != '\0';
    diag_count(local_ip_ok);
    network_diag_set_line(kNetworkDiagLocalIpLine,
                          kNetworkDiagLocalIpFormat,
                          local_ip_ok ? g_sta_ip : kNetworkDiagPlaceholder);

    network_diag_set_line(kNetworkDiagPublicIpLine, kNetworkDiagPublicIpFormat, kNetworkDiagStatusChecking);
    bool public_ip_ok = lookup_public_ip(public_ip, sizeof(public_ip));
    diag_count(public_ip_ok);
    network_diag_set_line(kNetworkDiagPublicIpLine,
                          kNetworkDiagPublicIpFormat,
                          public_ip_ok ? public_ip : kNetworkDiagStatusFailed);

    network_diag_set_line(kNetworkDiagDnsLine, kNetworkDiagDnsFormat, kNetworkDiagStatusChecking);
    bool dns_ok = dns_lookup_ok(kNetworkDiagQweatherDnsHost) && dns_lookup_ok(kNetworkDiagGithubDnsHost);
    network_diag_set_line(kNetworkDiagIpLocationLine, kNetworkDiagIpLocationFormat, kNetworkDiagStatusChecking);
    bool ip_ok = ip_geolocation_lookup(location, sizeof(location), city, sizeof(city));
    diag_count(ip_ok);
    network_diag_set_line(kNetworkDiagIpLocationLine, kNetworkDiagIpLocationCityFormat,
                          diag_result_text(ip_ok),
                          city[0] ? city : kNetworkDiagPlaceholder);

    diag_count(dns_ok);
    network_diag_set_result_line(kNetworkDiagDnsLine, kNetworkDiagDnsFormat, dns_ok);

    bool weather_ok = false;
    if (g_have_weather_key && !g_low_battery_mode) {
        network_diag_set_line(kNetworkDiagWeatherLine, kNetworkDiagWeatherFormat, kNetworkDiagStatusChecking);
        weather_ok = perform_weather_update();
    }
    network_diag_set_line(kNetworkDiagNtpLine, kNetworkDiagNtpFormat, kNetworkDiagStatusChecking);
    bool ntp_ok = perform_ntp_sync(kNetworkDiagNtpMaxRetries);
    diag_count(weather_ok);
    diag_count(ntp_ok);
    network_diag_set_result_line(kNetworkDiagWeatherLine, kNetworkDiagWeatherFormat, weather_ok);
    network_diag_set_result_line(kNetworkDiagNtpLine, kNetworkDiagNtpFormat, ntp_ok);

    network_diag_set_line(kNetworkDiagSayingLine, kNetworkDiagSayingFormat, kNetworkDiagStatusChecking);
    bool saying_ok = !g_low_battery_mode && perform_daily_saying_update();
    network_diag_set_line(kNetworkDiagInternetLine, kNetworkDiagInternetFormat, kNetworkDiagStatusChecking);
    bool internet_ok = public_ip_ok || http_probe_ok(kNetworkDiagPublicIpUrl, kNetworkDiagWideProbeBufferSize);
    diag_count(saying_ok);
    diag_count(internet_ok);
    network_diag_set_result_line(kNetworkDiagSayingLine, kNetworkDiagSayingFormat, saying_ok);
    network_diag_set_result_line(kNetworkDiagInternetLine, kNetworkDiagInternetFormat, internet_ok);

    network_diag_set_line(kNetworkDiagOtaLine, kNetworkDiagOtaFormat, kNetworkDiagStatusChecking);
    bool ota_ok = http_probe_ok(kOtaManifestUrl, kNetworkDiagWideProbeBufferSize);
    diag_count(ota_ok);
    network_diag_set_result_line(kNetworkDiagOtaLine, kNetworkDiagOtaFormat, ota_ok);

    network_diag_finish();
}
