// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#include "codex_usage_protocol.h"

#include <stdio.h>
#include <string.h>

namespace {
enum Field : uint16_t {
    V = 1U << 0, S = 1U << 1, T = 1U << 2, O = 1U << 3,
    R = 1U << 4, U = 1U << 5, Q = 1U << 6, D = 1U << 7,
    E = 1U << 8, W = 1U << 9, C = 1U << 10, X = 1U << 11,
    A = 1U << 12,
};
constexpr uint16_t kRequired = V | S | T | O | R | U | Q | D | W | C | X | A;

void skip_ws(const char *p, size_t n, size_t &i)
{
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) ++i;
}

bool parse_key(const char *p, size_t n, size_t &i, char &key)
{
    if (i + 3 > n || p[i++] != '"') return false;
    key = p[i++];
    return p[i++] == '"';
}

bool parse_integer(const char *p, size_t n, size_t &i, bool &negative, uint64_t &value)
{
    negative = false;
    value = 0;
    if (i < n && p[i] == '-') { negative = true; ++i; }
    if (i >= n || p[i] < '0' || p[i] > '9') return false;
    if (p[i] == '0' && i + 1 < n && p[i + 1] >= '0' && p[i + 1] <= '9') return false;
    while (i < n && p[i] >= '0' && p[i] <= '9') {
        const uint8_t digit = static_cast<uint8_t>(p[i++] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    return true;
}

uint16_t field_for(char key)
{
    switch (key) {
    case 'v': return V; case 's': return S; case 't': return T; case 'o': return O;
    case 'r': return R; case 'u': return U; case 'q': return Q; case 'd': return D;
    case 'e': return E; case 'w': return W; case 'c': return C; case 'x': return X;
    case 'a': return A; default: return 0;
    }
}

bool fits_u32(uint64_t v) { return v <= UINT32_MAX; }

bool append_unsigned(uint64_t value, char *buffer, size_t capacity,
                     size_t &offset)
{
    char reversed[20];
    size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    if (offset + count >= capacity) return false;
    while (count != 0) buffer[offset++] = reversed[--count];
    return true;
}
}

CodexUsageParseResult codex_usage_parse_status(const char *p, size_t n,
                                               CodexUsageSnapshot *out)
{
    if (!p || !out || n == 0) return CodexUsageParseResult::Malformed;
    if (n > kCodexUsageMaxPayloadBytes) return CodexUsageParseResult::TooLong;
    CodexUsageSnapshot parsed{};
    uint16_t seen = 0;
    uint64_t version = 0;
    size_t i = 0;
    skip_ws(p, n, i);
    if (i >= n || p[i++] != '{') return CodexUsageParseResult::Malformed;
    for (;;) {
        skip_ws(p, n, i);
        if (i < n && p[i] == '}') { ++i; break; }
        char key = 0;
        if (!parse_key(p, n, i, key)) return CodexUsageParseResult::Malformed;
        const uint16_t field = field_for(key);
        if (field == 0 || (seen & field) != 0) return CodexUsageParseResult::Malformed;
        skip_ws(p, n, i);
        if (i >= n || p[i++] != ':') return CodexUsageParseResult::Malformed;
        skip_ws(p, n, i);
        bool negative = false;
        uint64_t value = 0;
        if (!parse_integer(p, n, i, negative, value)) return CodexUsageParseResult::Malformed;
        if (negative && key != 'o') return CodexUsageParseResult::OutOfRange;
        switch (key) {
        case 'v': version = value; break;
        case 's': if (!fits_u32(value) || value == 0) return CodexUsageParseResult::OutOfRange; parsed.sequence = static_cast<uint32_t>(value); break;
        case 't': parsed.unix_time = value; break;
        case 'o': if (value > 840) return CodexUsageParseResult::OutOfRange; parsed.utc_offset_minutes = static_cast<int16_t>(negative ? -static_cast<int16_t>(value) : static_cast<int16_t>(value)); break;
        case 'r': if (value > 100) return CodexUsageParseResult::OutOfRange; parsed.remaining_percent = static_cast<uint8_t>(value); break;
        case 'u': if (!fits_u32(value)) return CodexUsageParseResult::OutOfRange; parsed.limit_window_minutes = static_cast<uint32_t>(value); break;
        case 'q': if (!fits_u32(value)) return CodexUsageParseResult::OutOfRange; parsed.quota_reset_seconds = static_cast<uint32_t>(value); break;
        case 'd': parsed.tokens_today = value; break;
        case 'e': if (value > 1) return CodexUsageParseResult::OutOfRange; parsed.tokens_today_estimated = value != 0; break;
        case 'w': parsed.tokens_7d = value; break;
        case 'c': if (value > UINT8_MAX) return CodexUsageParseResult::OutOfRange; parsed.reset_credits = static_cast<uint8_t>(value); break;
        case 'x': if (!fits_u32(value)) return CodexUsageParseResult::OutOfRange; parsed.next_credit_expiry_seconds = static_cast<uint32_t>(value); break;
        case 'a': if (value > UINT8_MAX) return CodexUsageParseResult::OutOfRange; parsed.active_threads = static_cast<uint8_t>(value); break;
        }
        seen |= field;
        skip_ws(p, n, i);
        if (i < n && p[i] == ',') {
            ++i;
            size_t next = i;
            skip_ws(p, n, next);
            if (next >= n || p[next] == '}') return CodexUsageParseResult::Malformed;
            continue;
        }
        if (i < n && p[i] == '}') { ++i; break; }
        return CodexUsageParseResult::Malformed;
    }
    skip_ws(p, n, i);
    if (i != n) return CodexUsageParseResult::Malformed;
    if ((seen & kRequired) != kRequired) return CodexUsageParseResult::MissingField;
    if (version != 1) return CodexUsageParseResult::UnsupportedVersion;
    *out = parsed;
    return CodexUsageParseResult::Ok;
}

uint32_t codex_usage_countdown_seconds(uint32_t seconds, uint32_t received, uint32_t now)
{
    const uint32_t elapsed = static_cast<uint32_t>(now - received) / 1000U;
    return elapsed >= seconds ? 0U : seconds - elapsed;
}

CodexUsageLinkState codex_usage_link_state(bool valid, bool connected,
                                           uint32_t last, uint32_t now)
{
    if (!valid) return CodexUsageLinkState::Waiting;
    return connected && static_cast<uint32_t>(now - last) <= kCodexUsageStaleMs
               ? CodexUsageLinkState::Linked : CodexUsageLinkState::Stale;
}

bool codex_usage_display_values_equal(const CodexUsageSnapshot &a,
                                      const CodexUsageSnapshot &b)
{
    return a.remaining_percent == b.remaining_percent &&
           a.tokens_today == b.tokens_today &&
           a.tokens_today_estimated == b.tokens_today_estimated &&
           a.tokens_7d == b.tokens_7d &&
           a.active_threads == b.active_threads &&
           a.reset_credits == b.reset_credits &&
           a.quota_reset_seconds == b.quota_reset_seconds &&
           a.next_credit_expiry_seconds == b.next_credit_expiry_seconds &&
           a.limit_window_minutes == b.limit_window_minutes &&
           a.unix_time == b.unix_time &&
           a.utc_offset_minutes == b.utc_offset_minutes;
}

bool codex_usage_format_tokens(uint64_t value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0) return false;
    size_t offset = 0;
    if (value < 1000) {
        if (!append_unsigned(value, buffer, capacity, offset)) return false;
    } else {
        const uint64_t unit = value < 1000000ULL ? 1000ULL :
                              value < 1000000000ULL ? 1000000ULL :
                              value < 1000000000000ULL ? 1000000000ULL :
                                                        1000000000000ULL;
        const char suffix = unit == 1000ULL ? 'K' :
                            unit == 1000000ULL ? 'M' :
                            unit == 1000000000ULL ? 'B' : 'T';
        const uint64_t rounded_tenths =
            (value / unit) * 10U + ((value % unit) * 10U + unit / 2U) / unit;
        if (!append_unsigned(rounded_tenths >= 100 ? (rounded_tenths + 5U) / 10U
                                                   : rounded_tenths / 10U,
                             buffer, capacity, offset)) return false;
        if (rounded_tenths < 100) {
            if (offset + 2 >= capacity) return false;
            buffer[offset++] = '.';
            buffer[offset++] = static_cast<char>('0' + rounded_tenths % 10U);
        }
        if (offset + 1 >= capacity) return false;
        buffer[offset++] = suffix;
    }
    buffer[offset] = '\0';
    return true;
}
