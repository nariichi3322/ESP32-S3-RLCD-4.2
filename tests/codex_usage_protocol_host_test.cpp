#include "codex_usage_protocol.h"

#include <assert.h>
#include <string.h>

int main()
{
    const char payload[] = "{\"v\":1,\"s\":42,\"t\":1784341234,\"o\":480,\"r\":68,\"u\":10080,\"q\":201600,\"d\":1250000,\"e\":1,\"w\":6840000,\"c\":2,\"x\":358400,\"a\":3}";
    CodexUsageSnapshot value{};
    assert(codex_usage_parse_status(payload, strlen(payload), &value) == CodexUsageParseResult::Ok);
    assert(value.sequence == 42 && value.remaining_percent == 68);
    assert(value.tokens_today == 1250000 && value.tokens_today_estimated);
    assert(value.tokens_7d == 6840000 && value.utc_offset_minutes == 480);
    const char no_estimate[] = "{\"v\":1,\"s\":1,\"t\":0,\"o\":-840,\"r\":0,\"u\":0,\"q\":0,\"d\":0,\"w\":0,\"c\":0,\"x\":0,\"a\":0}";
    assert(codex_usage_parse_status(no_estimate, strlen(no_estimate), &value) == CodexUsageParseResult::Ok);
    assert(!value.tokens_today_estimated && value.utc_offset_minutes == -840);
    assert(!value.secondary_available);
    const char payload_v2[] = "{\"v\":2,\"s\":2,\"t\":0,\"o\":0,\"r\":80,\"u\":300,\"q\":60,\"n\":1,\"R\":65,\"U\":43200,\"Q\":120,\"d\":1,\"e\":0,\"w\":2,\"c\":0,\"x\":0,\"a\":0}";
    assert(codex_usage_parse_status(payload_v2, strlen(payload_v2), &value) == CodexUsageParseResult::Ok);
    assert(value.secondary_available && value.secondary_remaining_percent == 65);
    assert(value.secondary_limit_window_minutes == 43200);
    assert(value.secondary_quota_reset_seconds == 120);
    const char inconsistent_v2[] = "{\"v\":2,\"s\":2,\"t\":0,\"o\":0,\"r\":80,\"u\":300,\"q\":60,\"n\":0,\"R\":65,\"U\":0,\"Q\":0,\"d\":1,\"e\":0,\"w\":2,\"c\":0,\"x\":0,\"a\":0}";
    assert(codex_usage_parse_status(inconsistent_v2, strlen(inconsistent_v2), &value) == CodexUsageParseResult::OutOfRange);
    const char missing[] = "{\"v\":1}";
    assert(codex_usage_parse_status(missing, strlen(missing), &value) == CodexUsageParseResult::MissingField);
    const char bad_version[] = "{\"v\":3,\"s\":1,\"t\":0,\"o\":0,\"r\":0,\"u\":0,\"q\":0,\"d\":0,\"w\":0,\"c\":0,\"x\":0,\"a\":0}";
    assert(codex_usage_parse_status(bad_version, strlen(bad_version), &value) == CodexUsageParseResult::UnsupportedVersion);
    const char bad_range[] = "{\"v\":1,\"s\":1,\"t\":0,\"o\":0,\"r\":101,\"u\":0,\"q\":0,\"d\":0,\"w\":0,\"c\":0,\"x\":0,\"a\":0}";
    assert(codex_usage_parse_status(bad_range, strlen(bad_range), &value) == CodexUsageParseResult::OutOfRange);
    const char trailing_comma[] = "{\"v\":1,}";
    assert(codex_usage_parse_status(trailing_comma, strlen(trailing_comma), &value) == CodexUsageParseResult::Malformed);
    char oversized[kCodexUsageMaxPayloadBytes + 1] = {};
    assert(codex_usage_parse_status(oversized, sizeof(oversized), &value) == CodexUsageParseResult::TooLong);
    assert(codex_usage_countdown_seconds(120, 1000, 61000) == 60);
    assert(codex_usage_countdown_seconds(120, 1000, 121000) == 0);
    assert(codex_usage_link_state(false, false, false, 0, 0) == CodexUsageLinkState::Disconnected);
    assert(codex_usage_link_state(false, true, false, 0, 0) == CodexUsageLinkState::Waiting);
    assert(codex_usage_link_state(true, true, true, 100, 60100) == CodexUsageLinkState::Linked);
    assert(codex_usage_link_state(true, true, true, 100, 60101) == CodexUsageLinkState::Stale);
    assert(codex_usage_link_state(true, false, false, 100, 101) == CodexUsageLinkState::Disconnected);
    char text[24];
    assert(codex_usage_format_tokens(999, text, sizeof(text)) && strcmp(text, "999") == 0);
    assert(codex_usage_format_tokens(1250, text, sizeof(text)) && strcmp(text, "1.3K") == 0);
    assert(codex_usage_format_window(300, text, sizeof(text)) && strcmp(text, "5h") == 0);
    assert(codex_usage_format_window(10080, text, sizeof(text)) && strcmp(text, "7d") == 0);
    assert(codex_usage_format_window(43200, text, sizeof(text)) && strcmp(text, "30d") == 0);
    assert(codex_usage_format_window(44640, text, sizeof(text)) && strcmp(text, "31d") == 0);
    assert(codex_usage_format_tokens(6840000, text, sizeof(text)) && strcmp(text, "6.8M") == 0);
    assert(codex_usage_format_tokens(1240000000, text, sizeof(text)) && strcmp(text, "1.2B") == 0);
    assert(codex_usage_format_tokens(UINT64_MAX, text, sizeof(text)) && strchr(text, 'T'));
    return 0;
}
