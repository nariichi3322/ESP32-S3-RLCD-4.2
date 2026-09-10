// Validates and normalizes the user-configurable NTP server host name.
#include "ntp_server_config.h"

#include "ascii_text.h"
#include "app_text_format.h"

#include <ctype.h>
#include <string.h>

namespace {
bool ntp_label_valid(const char *start, size_t len)
{
    if (!start || len == 0 || len > 63 ||
        start[0] == '-' || start[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(start[i]);
        if (!isalnum(ch) && ch != '-') {
            return false;
        }
    }
    return true;
}
} // namespace

bool normalize_ntp_server_name(const char *input,
                               char *out,
                               size_t out_len)
{
    if (!app_text::output_buffer_available(out, out_len) ||
        out_len < kNtpServerNameLen) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }
    char candidate[kNtpServerNameLen] = {};
    const char *source = input ? input : "";
    const size_t source_len = strnlen(source, sizeof(candidate));
    if (source_len >= sizeof(candidate)) {
        out[0] = '\0';
        return false;
    }
    memcpy(candidate, source, source_len);
    trim_ascii_whitespace(candidate);
    const size_t length = strlen(candidate);
    if (length == 0 || length >= kNtpServerNameLen) {
        out[0] = '\0';
        return false;
    }
    const char *label = candidate;
    for (const char *cursor = candidate;; ++cursor) {
        if (*cursor != '.' && *cursor != '\0') {
            continue;
        }
        if (!ntp_label_valid(label, static_cast<size_t>(cursor - label))) {
            out[0] = '\0';
            return false;
        }
        if (*cursor == '\0') {
            break;
        }
        label = cursor + 1;
    }
    memcpy(out, candidate, length + 1);
    return true;
}
