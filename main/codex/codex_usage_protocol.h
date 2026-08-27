// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#pragma once

#include <stddef.h>
#include <stdint.h>

inline constexpr size_t kCodexUsageMaxPayloadBytes = 256;
inline constexpr uint32_t kCodexUsageStaleMs = 60000;

enum class CodexUsageLinkState : uint8_t {
    Waiting,
    Linked,
    Stale,
};

struct CodexUsageSnapshot {
    uint8_t remaining_percent = 0;
    uint64_t tokens_today = 0;
    bool tokens_today_estimated = false;
    uint64_t tokens_7d = 0;
    uint8_t active_threads = 0;
    uint8_t reset_credits = 0;
    uint32_t quota_reset_seconds = 0;
    uint32_t next_credit_expiry_seconds = 0;
    uint32_t limit_window_minutes = 0;
    uint64_t unix_time = 0;
    int16_t utc_offset_minutes = 0;
    uint32_t sequence = 0;
};

enum class CodexUsageParseResult : uint8_t {
    Ok,
    TooLong,
    Malformed,
    MissingField,
    UnsupportedVersion,
    OutOfRange,
};

CodexUsageParseResult codex_usage_parse_status(const char *payload,
                                               size_t payload_size,
                                               CodexUsageSnapshot *out);
uint32_t codex_usage_countdown_seconds(uint32_t received_seconds,
                                       uint32_t received_tick_ms,
                                       uint32_t now_tick_ms);
CodexUsageLinkState codex_usage_link_state(bool data_valid,
                                           bool ble_connected,
                                           uint32_t last_valid_tick_ms,
                                           uint32_t now_tick_ms);
bool codex_usage_display_values_equal(const CodexUsageSnapshot &a,
                                      const CodexUsageSnapshot &b);
bool codex_usage_format_tokens(uint64_t tokens, char *buffer, size_t capacity);

