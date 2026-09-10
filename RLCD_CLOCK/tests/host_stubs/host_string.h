#pragma once

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

inline size_t host_strlcpy(char *dst, const char *src, size_t size)
{
    const size_t length = strlen(src);
    if (size != 0) {
        const size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copied);
        dst[copied] = '\0';
    }
    return length;
}

inline size_t host_strlcat(char *dst, const char *src, size_t size)
{
    const size_t dst_length = strnlen(dst, size);
    if (dst_length == size) return size + strlen(src);
    return dst_length + host_strlcpy(dst + dst_length, src, size - dst_length);
}

#ifdef _WIN32
inline int host_setenv(const char *name, const char *value, int)
{
    return _putenv_s(name, value);
}

inline tm *host_localtime_r(const time_t *value, tm *out)
{
    return localtime_s(out, value) == 0 ? out : nullptr;
}
#endif
