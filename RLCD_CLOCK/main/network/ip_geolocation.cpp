// 调用独立 IP 定位服务并转换为天气城市查询所需的位置文本。
#include "ip_geolocation_client.h"
#include "ip_geolocation_parser.h"

#include "network_http_client.h"

#include "app_constexpr.h"
#include "app_metadata.h"
#include "app_text_format.h"
#include "ui_language.h"

#include "scoped_heap_buffer.h"

#include "esp_log.h"

#include <cstdio>

namespace {
constexpr size_t kIpGeoResponseBufferSize = 2048;
constexpr const char *kIpGeolocationUrlFormat =
    "https://ipwho.is/?fields=success,city,latitude,longitude&lang=%s";
constexpr size_t kIpGeolocationUrlSize =
    sizeof("https://ipwho.is/?fields=success,city,latitude,longitude&lang=zh-CN");
constexpr const char *kIpGeoStage = "ip location";
constexpr const char *kIpLocationInvalidArgLog = "ip location invalid argument";
#define IP_LOCATION_RESOLVED_FORMAT "ip location resolved: %s city=%s"

const char *ipwhois_language_tag()
{
    switch (ui_language_load()) {
    case UiLanguage::English: return "en";
    case UiLanguage::Japanese: return "ja";
    default: return "zh-CN";
    }
}

bool format_ipwhois_url(char *out, size_t out_len)
{
    const int written = snprintf(out, out_len, kIpGeolocationUrlFormat,
                                 ipwhois_language_tag());
    return written > 0 && static_cast<size_t>(written) < out_len;
}

void log_ip_geolocation_warning(const char *message)
{
    ESP_LOGW(TAG, "%s", cstr_nonempty(message) ? message : kIpGeoStage);
}

static_assert(kIpGeoResponseBufferSize > 1,
              "IP geolocation response buffer must fit text and NUL");
} // namespace

bool ip_geolocation_lookup(char *location, size_t location_len, char *city, size_t city_len)
{
    if (!app_text::output_buffer_available(location, location_len) ||
        !app_text::output_buffer_available(city, city_len)) {
        log_ip_geolocation_warning(kIpLocationInvalidArgLog);
        return false;
    }
    ScopedHeapBuffer<char> response(kIpGeoResponseBufferSize,
                                    HeapBufferInit::kZeroed,
                                    HeapBufferStorage::kPsramRequired);
    char url[kIpGeolocationUrlSize] = {};
    if (!response || !format_ipwhois_url(url, sizeof(url)) ||
        http_get_text(url, response.data(), response.size()) != ESP_OK) {
        return false;
    }

    if (!parse_ipwhois_response(response.data(), location, location_len, city, city_len)) {
        return false;
    }
    ESP_LOGI(TAG, IP_LOCATION_RESOLVED_FORMAT, location, city);
    return true;
}
