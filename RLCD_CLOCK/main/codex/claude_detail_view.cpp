#include "claude_detail_view.h"

ClaudeDetailView claude_detail_make_view(const ClaudeDetailSnapshot &snapshot,
                                         bool valid,
                                         uint32_t received_tick_ms,
                                         uint32_t now_tick_ms)
{
    ClaudeDetailView view{};
    if (!valid) return view;
    if (snapshot.captured_at != 0 && snapshot.sent_at >= snapshot.captured_at) {
        const uint64_t elapsed = static_cast<uint32_t>(now_tick_ms - received_tick_ms) / 1000U;
        const uint64_t initial_age = snapshot.sent_at - snapshot.captured_at;
        view.age_known = true;
        view.age_seconds = initial_age > UINT64_MAX - elapsed
                               ? UINT64_MAX : initial_age + elapsed;
    }
    if (snapshot.source_state == 2 || snapshot.source_state == 3) {
        view.fable = ClaudeDetailFableState::NotLoggedIn;
        view.spend = ClaudeDetailSpendState::NotLoggedIn;
        view.grants = ClaudeDetailGrantsState::NotLoggedIn;
        return view;
    }
    if (!view.age_known || view.age_seconds > 3600) return view;

    if ((snapshot.flags & ClaudeDetailFableKnown) != 0) {
        if ((snapshot.flags & ClaudeDetailFablePresent) == 0) {
            view.fable = ClaudeDetailFableState::NoPlan;
        } else {
            view.fable = ClaudeDetailFableState::Value;
            const uint32_t used = (static_cast<uint32_t>(snapshot.fable_used_bp) + 99U) / 100U;
            view.fable_remaining_percent = static_cast<uint8_t>(used >= 100U ? 0U : 100U - used);
            view.fable_resets_at = snapshot.fable_resets_at;
        }
    }
    if ((snapshot.flags & ClaudeDetailSpendKnown) != 0) {
        if ((snapshot.flags & ClaudeDetailSpendEnabled) == 0) {
            view.spend = ClaudeDetailSpendState::Disabled;
        } else if ((snapshot.flags & ClaudeDetailAmountValid) != 0 &&
                   snapshot.exponent <= 9 && snapshot.currency[0] >= 'A' &&
                   snapshot.currency[0] <= 'Z' && snapshot.currency[1] >= 'A' &&
                   snapshot.currency[1] <= 'Z' && snapshot.currency[2] >= 'A' &&
                   snapshot.currency[2] <= 'Z') {
            view.spend = ClaudeDetailSpendState::Value;
            for (size_t i = 0; i < 3; ++i) view.currency[i] = snapshot.currency[i];
            view.exponent = snapshot.exponent;
            view.spend_used = snapshot.spend_used;
            view.spend_limit = snapshot.spend_limit;
        }
    }
    if ((snapshot.flags & ClaudeDetailGrantsKnown) != 0) {
        if (snapshot.grant_count == 0) {
            view.grants = ClaudeDetailGrantsState::None;
        } else {
            view.grants = ClaudeDetailGrantsState::Value;
            view.grant_count = snapshot.grant_count <= 2 ? snapshot.grant_count : 2;
            view.more = (snapshot.flags & ClaudeDetailMoreGrants) != 0;
            for (size_t i = 0; i < view.grant_count; ++i) {
                view.grant_rows[i].type = snapshot.grants[i].type;
                view.grant_rows[i].resets_left = snapshot.grants[i].resets_left;
                view.grant_rows[i].ends_at = snapshot.grants[i].ends_at;
            }
        }
    }
    return view;
}
