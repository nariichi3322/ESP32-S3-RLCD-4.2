// 声明小智协议使用的 UTF-8 文本和 WebSocket URL 纯解析工具。
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace xiaozhi_protocol {

static __attribute__((noinline)) bool output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

static __attribute__((noinline)) size_t utf8_character_size(const unsigned char *text, size_t remaining)
{
    if (!text || remaining == 0) {
        return 0;
    }
    size_t size = 1;
    if (text[0] < 0x80) {
        return 1;
    }
    if ((text[0] & 0xe0) == 0xc0) {
        size = 2;
    } else if ((text[0] & 0xf0) == 0xe0) {
        size = 3;
    } else if ((text[0] & 0xf8) == 0xf0) {
        size = 4;
    } else {
        return 0;
    }
    if (size > remaining) {
        return 0;
    }
    for (size_t index = 1; index < size; ++index) {
        if ((text[index] & 0xc0) != 0x80) {
            return 0;
        }
    }
    return size;
}

static __attribute__((noinline)) void utf8_safe_copy(char *out, size_t out_len, const char *text)
{
    if (!output_buffer_available(out, out_len)) {
        return;
    }
    out[0] = '\0';
    if (!text) {
        return;
    }
    const unsigned char *source = reinterpret_cast<const unsigned char *>(text);
    size_t source_len = strlen(text);
    size_t source_offset = 0;
    size_t output_offset = 0;
    while (source_offset < source_len && output_offset + 1 < out_len) {
        size_t character_size = utf8_character_size(source + source_offset, source_len - source_offset);
        if (character_size == 0) {
            out[output_offset++] = '?';
            ++source_offset;
            continue;
        }
        if (output_offset + character_size >= out_len) {
            break;
        }
        memcpy(out + output_offset, source + source_offset, character_size);
        output_offset += character_size;
        source_offset += character_size;
    }
    out[output_offset] = '\0';
}

static __attribute__((noinline)) bool parse_websocket_url(const char *url,
                                bool *secure,
                                char *host,
                                size_t host_len,
                                int *port,
                                char *path,
                                size_t path_len)
{
    if (!url || !secure || !output_buffer_available(host, host_len) || !port ||
        !output_buffer_available(path, path_len)) {
        return false;
    }
    const char *cursor = nullptr;
    if (strncmp(url, "wss://", 6) == 0) {
        *secure = true;
        cursor = url + 6;
        *port = 443;
    } else if (strncmp(url, "ws://", 5) == 0) {
        *secure = false;
        cursor = url + 5;
        *port = 80;
    } else {
        return false;
    }
    const char *path_start = strchr(cursor, '/');
    const char *host_end = path_start ? path_start : cursor + strlen(cursor);
    const char *colon = nullptr;
    for (const char *it = cursor; it < host_end; ++it) {
        if (*it == ':') {
            colon = it;
        }
    }
    size_t copied_host = static_cast<size_t>((colon ? colon : host_end) - cursor);
    if (copied_host == 0 || copied_host >= host_len) {
        return false;
    }
    memcpy(host, cursor, copied_host);
    host[copied_host] = '\0';
    if (colon) {
        char *end = nullptr;
        long parsed_port = strtol(colon + 1, &end, 10);
        if (end != host_end || parsed_port <= 0 || parsed_port > 65535) {
            return false;
        }
        *port = static_cast<int>(parsed_port);
    }
    strlcpy(path, path_start ? path_start : "/", path_len);
    return true;
}

} // namespace xiaozhi_protocol
