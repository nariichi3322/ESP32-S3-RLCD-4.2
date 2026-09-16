// 宣告 ipwho.is IP 定位回應的純文字解析器。
#pragma once

#include <stddef.h>

bool parse_ipwhois_response(const char *json,
                            char *location,
                            size_t location_len,
                            char *city,
                            size_t city_len);
