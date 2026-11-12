#pragma once

#include <stddef.h>
#include <stdint.h>

inline constexpr size_t kClaudeDetailFrameBytes = 104;
inline constexpr size_t kClaudeDetailMaxPayloadBytes = 180;

enum ClaudeDetailFlag : uint8_t {
    ClaudeDetailFableKnown = 1U << 0,
    ClaudeDetailFablePresent = 1U << 1,
    ClaudeDetailSpendKnown = 1U << 2,
    ClaudeDetailSpendEnabled = 1U << 3,
    ClaudeDetailAmountValid = 1U << 4,
    ClaudeDetailGrantsKnown = 1U << 5,
    ClaudeDetailMoreGrants = 1U << 6,
};

enum class ClaudeDetailGrantType : uint8_t {
    Other = 0,
    Full = 1,
    Fable = 2,
    Partial = 3,
};

struct ClaudeDetailGrant {
    ClaudeDetailGrantType type = ClaudeDetailGrantType::Other;
    uint8_t clears = 0;
    uint8_t state = 0;
    uint64_t resets_left = 0;
    uint64_t resets_total = 0;
    uint64_t ends_at = 0;
};

struct ClaudeDetailSnapshot {
    uint8_t version = 0;
    uint8_t source_state = 0;
    uint8_t flags = 0;
    uint8_t grant_count = 0;
    uint64_t captured_at = 0;
    uint64_t sent_at = 0;
    uint16_t fable_used_bp = 0;
    uint64_t fable_resets_at = 0;
    uint8_t currency[3]{};
    uint8_t exponent = 0;
    uint64_t spend_used = 0;
    uint64_t spend_limit = 0;
    ClaudeDetailGrant grants[2]{};
};

enum class ClaudeDetailParseResult : uint8_t {
    Ok,
    InvalidLength,
    UnsupportedVersion,
    OutOfRange,
    Malformed,
    Unavailable,
};

ClaudeDetailParseResult claude_detail_parse(const uint8_t *payload,
                                            size_t payload_size,
                                            ClaudeDetailSnapshot *out);
