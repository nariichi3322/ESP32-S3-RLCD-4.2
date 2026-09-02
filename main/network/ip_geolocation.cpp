// 调用独立 IP 定位服务并转换为天气城市查询所需的位置文本。
#include "ip_geolocation_client.h"
#include "ip_region_text.h"

#include "network_http_client.h"

#include "app_constexpr.h"
#include "app_metadata.h"
#include "app_text_format.h"

#include "cJSON.h"
#include "scoped_heap_buffer.h"

#include "esp_log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t kIpGeoResponseBufferSize = 2048;
constexpr const char *kIpGeolocationUrl = "https://uapis.cn/api/v1/network/myip";
constexpr const char *kIpGeoStage = "ip location";
constexpr const char *kIpGeoJsonLatitudeField = "latitude";
constexpr const char *kIpGeoJsonLongitudeField = "longitude";
constexpr const char *kIpGeoJsonRegionField = "region";
constexpr const char *kIpLocationInvalidArgLog = "ip location invalid argument";
constexpr const char *kIpLocationMissingCoordinateLog =
    "ip location response missing latitude/longitude";
#define IP_LOCATION_RESOLVED_FORMAT "ip location resolved: %s city=%s"

bool coordinates_available(const cJSON *latitude, const cJSON *longitude)
{
    return cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude) &&
           std::isfinite(latitude->valuedouble) &&
           std::isfinite(longitude->valuedouble) &&
           latitude->valuedouble >= -90.0 && latitude->valuedouble <= 90.0 &&
           longitude->valuedouble >= -180.0 && longitude->valuedouble <= 180.0;
}

bool format_coordinates(char *out, size_t out_len, double longitude, double latitude)
{
    const int written = snprintf(out, out_len, "%.6f,%.6f", longitude, latitude);
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
    if (!response ||
        http_get_text(kIpGeolocationUrl, response.data(), response.size()) != ESP_OK) {
        return false;
    }
    cJSON *root = cJSON_Parse(response.data());
    if (!root) {
        return false;
    }
    const cJSON *latitude = cJSON_GetObjectItem(root, kIpGeoJsonLatitudeField);
    const cJSON *longitude = cJSON_GetObjectItem(root, kIpGeoJsonLongitudeField);
    const cJSON *region = cJSON_GetObjectItem(root, kIpGeoJsonRegionField);
    if (!coordinates_available(latitude, longitude)) {
        cJSON_Delete(root);
        log_ip_geolocation_warning(kIpLocationMissingCoordinateLog);
        return false;
    }
    if (!format_coordinates(location, location_len,
                            longitude->valuedouble, latitude->valuedouble)) {
        cJSON_Delete(root);
        return false;
    }
    const char *region_text = cJSON_IsString(region) ? region->valuestring : nullptr;
    if (region_text) {
        (void)normalize_ip_region_city(region_text, city, city_len);
    }
    if (city[0] == '\0') {
        strlcpy(city, location, city_len);
    }
    ESP_LOGI(TAG, IP_LOCATION_RESOLVED_FORMAT, location, city);
    cJSON_Delete(root);
    return true;
}
