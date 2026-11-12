#include "claude_detail_protocol.h"

namespace {
uint64_t read_le(const uint8_t *bytes, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8U);
    }
    return value;
}
}

ClaudeDetailParseResult claude_detail_parse(const uint8_t *payload,
                                            size_t payload_size,
                                            ClaudeDetailSnapshot *out)
{
    if (payload_size < kClaudeDetailFrameBytes ||
        payload_size > kClaudeDetailMaxPayloadBytes) {
        return ClaudeDetailParseResult::InvalidLength;
    }
    if (!payload || !out) return ClaudeDetailParseResult::Malformed;
    if (payload[0] != 1) return ClaudeDetailParseResult::UnsupportedVersion;
    if (payload[1] > 5 || (payload[2] & 0x80U) != 0 || payload[3] > 2 ||
        read_le(payload + 20, 2) > 10000 ||
        (payload[33] > 9 && payload[33] != 255)) {
        return ClaudeDetailParseResult::OutOfRange;
    }
    const bool currency_zero = payload[30] == 0 && payload[31] == 0 &&
                               payload[32] == 0;
    const bool currency_upper = payload[30] >= 'A' && payload[30] <= 'Z' &&
                                payload[31] >= 'A' && payload[31] <= 'Z' &&
                                payload[32] >= 'A' && payload[32] <= 'Z';
    if (!currency_zero && !currency_upper) return ClaudeDetailParseResult::OutOfRange;

    ClaudeDetailSnapshot parsed{};
    parsed.version = payload[0];
    parsed.source_state = payload[1];
    parsed.flags = payload[2];
    parsed.grant_count = payload[3];
    parsed.captured_at = read_le(payload + 4, 8);
    parsed.sent_at = read_le(payload + 12, 8);
    parsed.fable_used_bp = static_cast<uint16_t>(read_le(payload + 20, 2));
    parsed.fable_resets_at = read_le(payload + 22, 8);
    for (size_t i = 0; i < 3; ++i) parsed.currency[i] = payload[30 + i];
    parsed.exponent = payload[33];
    parsed.spend_used = read_le(payload + 34, 8);
    parsed.spend_limit = read_le(payload + 42, 8);
    for (size_t i = 0; i < 2; ++i) {
        const uint8_t *grant = payload + 50 + i * 27;
        if (grant[0] > 3 || (grant[1] & 0xf0U) != 0 ||
            (grant[2] & 0xfcU) != 0) {
            return ClaudeDetailParseResult::OutOfRange;
        }
        parsed.grants[i].type = static_cast<ClaudeDetailGrantType>(grant[0]);
        parsed.grants[i].clears = grant[1];
        parsed.grants[i].state = grant[2];
        parsed.grants[i].resets_left = read_le(grant + 3, 8);
        parsed.grants[i].resets_total = read_le(grant + 11, 8);
        parsed.grants[i].ends_at = read_le(grant + 19, 8);
    }
    *out = parsed;
    return ClaudeDetailParseResult::Ok;
}
