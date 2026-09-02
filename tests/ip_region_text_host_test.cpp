#include "ip_region_text.h"

#include <assert.h>
#include <string.h>

int main()
{
    char city[32] = {};
    assert(normalize_ip_region_city("中國 台灣 新北市", city, sizeof(city)));
    assert(strcmp(city, "新北") == 0);
    assert(normalize_ip_region_city("中國  台灣\t台中市  ", city, sizeof(city)));
    assert(strcmp(city, "台中") == 0);
    assert(normalize_ip_region_city("臺北市", city, sizeof(city)));
    assert(strcmp(city, "臺北") == 0);
    assert(normalize_ip_region_city("中國 台灣", city, sizeof(city)));
    assert(strcmp(city, "台灣") == 0);
    assert(!normalize_ip_region_city("   ", city, sizeof(city)));
    assert(city[0] == '\0');
    char small[4] = {};
    assert(!normalize_ip_region_city("中國 台灣 新北市", small, sizeof(small)));
    assert(small[0] == '\0');
    return 0;
}
