// 解析 ipwho.is 的城市與座標回應。
#include "ip_geolocation_parser.h"

#include "network_json_root.h"

#include "cJSON.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t kCoordinateTextSize = sizeof("-180.000000,-90.000000");

bool coordinates_available(const cJSON *latitude, const cJSON *longitude)
{
    return cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude) &&
           std::isfinite(latitude->valuedouble) &&
           std::isfinite(longitude->valuedouble) &&
           latitude->valuedouble >= -90.0 && latitude->valuedouble <= 90.0 &&
           longitude->valuedouble >= -180.0 && longitude->valuedouble <= 180.0;
}
} // namespace

bool parse_ipwhois_response(const char *json,
                            char *location,
                            size_t location_len,
                            char *city,
                            size_t city_len)
{
    if (!location || location_len == 0 || !city || city_len == 0) {
        return false;
    }
    location[0] = '\0';
    city[0] = '\0';

    NetworkJsonRoot root(json);
    if (!root) {
        return false;
    }

    const cJSON *success = cJSON_GetObjectItemCaseSensitive(root.get(), "success");
    const cJSON *latitude = cJSON_GetObjectItemCaseSensitive(root.get(), "latitude");
    const cJSON *longitude = cJSON_GetObjectItemCaseSensitive(root.get(), "longitude");
    if (!cJSON_IsTrue(success) || !coordinates_available(latitude, longitude)) {
        return false;
    }

    char formatted_location[kCoordinateTextSize] = {};
    const int written = snprintf(formatted_location,
                                 sizeof(formatted_location),
                                 "%.6f,%.6f",
                                 longitude->valuedouble,
                                 latitude->valuedouble);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(formatted_location) ||
        static_cast<size_t>(written) >= location_len) {
        return false;
    }

    const cJSON *city_item = cJSON_GetObjectItemCaseSensitive(root.get(), "city");
    const char *city_text = cJSON_IsString(city_item) && city_item->valuestring ?
                                city_item->valuestring :
                                nullptr;
    if (!city_text || city_text[0] == '\0' || strlen(city_text) >= city_len) {
        city_text = formatted_location;
    }
    if (strlen(city_text) >= city_len) {
        return false;
    }

    strlcpy(location, formatted_location, location_len);
    strlcpy(city, city_text, city_len);
    return true;
}
