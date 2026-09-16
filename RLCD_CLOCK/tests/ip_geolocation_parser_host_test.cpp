// 驗證 ipwho.is 城市與座標回應解析、語系文字和錯誤邊界。
#include "ip_geolocation_parser.h"

#include <assert.h>
#include <string.h>

namespace {
void expect_ok(const char *json, const char *expected_location, const char *expected_city)
{
    char location[32] = "stale-location";
    char city[32] = "stale-city";
    assert(parse_ipwhois_response(json,
                                  location,
                                  sizeof(location),
                                  city,
                                  sizeof(city)));
    assert(strcmp(location, expected_location) == 0);
    assert(strcmp(city, expected_city) == 0);
}

void expect_rejected(const char *json)
{
    char location[32] = "stale-location";
    char city[32] = "stale-city";
    assert(!parse_ipwhois_response(json,
                                   location,
                                   sizeof(location),
                                   city,
                                   sizeof(city)));
    assert(location[0] == '\0');
    assert(city[0] == '\0');
}
} // namespace

int main()
{
    expect_ok(R"({"success":true,"city":"Taipei","latitude":25.0530572,"longitude":121.5263893})",
              "121.526389,25.053057",
              "Taipei");
    expect_ok(R"({"success":true,"city":"臺北","latitude":25.0530572,"longitude":121.5263893})",
              "121.526389,25.053057",
              "臺北");
    expect_ok(R"({"success":true,"city":"台北","latitude":25.0530572,"longitude":121.5263893})",
              "121.526389,25.053057",
              "台北");
    expect_ok(R"({"success":true,"latitude":25.0530572,"longitude":121.5263893})",
              "121.526389,25.053057",
              "121.526389,25.053057");
    expect_ok(R"({"success":true,"city":"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456789","latitude":25.0530572,"longitude":121.5263893})",
              "121.526389,25.053057",
              "121.526389,25.053057");

    expect_rejected(R"({"success":false,"message":"Rate limit exceeded"})");
    expect_rejected(R"({"city":"Taipei","latitude":25.0530572,"longitude":121.5263893})");
    expect_rejected(R"({"success":true,"city":"Taipei","latitude":91,"longitude":121.5263893})");
    expect_rejected(R"({"success":true,"city":"Taipei","latitude":25.0530572,"longitude":181})");
    expect_rejected("{");
    expect_rejected(nullptr);

    char location[32] = "keep";
    char city[32] = "keep";
    assert(!parse_ipwhois_response(
        R"({"success":true,"city":"Taipei","latitude":25.0530572,"longitude":121.5263893})",
        nullptr,
        sizeof(location),
        city,
        sizeof(city)));
    assert(strcmp(city, "keep") == 0);
    assert(!parse_ipwhois_response(
        R"({"success":true,"city":"Taipei","latitude":25.0530572,"longitude":121.5263893})",
        location,
        sizeof(location),
        city,
        0));
    assert(strcmp(location, "keep") == 0);
    return 0;
}
