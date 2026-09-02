#pragma once

#include <stddef.h>
#include <string.h>

constexpr bool ip_region_ascii_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

inline bool normalize_ip_region_city(const char *region,
                                     char *out,
                                     size_t out_len)
{
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (!region) return false;

    const char *end = region + strlen(region);
    while (end > region && ip_region_ascii_space(end[-1])) --end;
    if (end == region) return false;

    const char *start = end;
    while (start > region && !ip_region_ascii_space(start[-1])) --start;
    const size_t length = static_cast<size_t>(end - start);
    if (length == 0 || length >= out_len) return false;
    memcpy(out, start, length);
    out[length] = '\0';

    constexpr char kCitySuffix[] = "市";
    constexpr size_t kCitySuffixLen = sizeof(kCitySuffix) - 1U;
    if (length >= kCitySuffixLen &&
        memcmp(out + length - kCitySuffixLen,
               kCitySuffix,
               kCitySuffixLen) == 0) {
        out[length - kCitySuffixLen] = '\0';
    }
    return out[0] != '\0';
}
