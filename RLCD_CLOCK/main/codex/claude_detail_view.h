#pragma once

#include "claude_detail_protocol.h"

enum class ClaudeDetailFableState : uint8_t {
    Unknown,
    NotLoggedIn,
    NoPlan,
    Value,
};

enum class ClaudeDetailSpendState : uint8_t {
    Unknown,
    NotLoggedIn,
    Disabled,
    Value,
};

enum class ClaudeDetailGrantsState : uint8_t {
    Unknown,
    NotLoggedIn,
    None,
    Value,
};

struct ClaudeDetailGrantRow {
    ClaudeDetailGrantType type = ClaudeDetailGrantType::Other;
    uint64_t resets_left = 0;
    uint64_t ends_at = 0;
};

struct ClaudeDetailView {
    ClaudeDetailFableState fable = ClaudeDetailFableState::Unknown;
    uint8_t fable_remaining_percent = 0;
    uint64_t fable_resets_at = 0;
    ClaudeDetailSpendState spend = ClaudeDetailSpendState::Unknown;
    uint8_t currency[3]{};
    uint8_t exponent = 0;
    uint64_t spend_used = 0;
    uint64_t spend_limit = 0;
    ClaudeDetailGrantsState grants = ClaudeDetailGrantsState::Unknown;
    uint8_t grant_count = 0;
    ClaudeDetailGrantRow grant_rows[2]{};
    bool more = false;
    bool age_known = false;
    uint64_t age_seconds = 0;
};

ClaudeDetailView claude_detail_make_view(const ClaudeDetailSnapshot &snapshot,
                                         bool valid,
                                         uint32_t received_tick_ms,
                                         uint32_t now_tick_ms);
