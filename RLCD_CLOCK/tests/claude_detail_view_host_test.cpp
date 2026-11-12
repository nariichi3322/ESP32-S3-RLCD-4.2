#include "claude_detail_view.h"

#include <assert.h>
#include <initializer_list>

namespace {
ClaudeDetailSnapshot snapshot()
{
    ClaudeDetailSnapshot data{};
    data.version = 1;
    data.source_state = 1;
    data.flags = ClaudeDetailFableKnown | ClaudeDetailFablePresent |
                 ClaudeDetailSpendKnown | ClaudeDetailSpendEnabled |
                 ClaudeDetailAmountValid | ClaudeDetailGrantsKnown;
    data.captured_at = 100;
    data.sent_at = 128;
    data.fable_used_bp = 1200;
    data.fable_resets_at = 123456;
    data.currency[0] = 'U';
    data.currency[1] = 'S';
    data.currency[2] = 'D';
    data.exponent = 2;
    data.spend_used = 786;
    data.spend_limit = 4000;
    data.grant_count = 2;
    data.grants[0].type = ClaudeDetailGrantType::Full;
    data.grants[0].resets_left = 1;
    data.grants[0].ends_at = 1000;
    data.grants[1].type = ClaudeDetailGrantType::Fable;
    data.grants[1].resets_left = 2;
    data.grants[1].ends_at = 2000;
    return data;
}

void all_unknown(const ClaudeDetailView &view)
{
    assert(view.fable == ClaudeDetailFableState::Unknown);
    assert(view.spend == ClaudeDetailSpendState::Unknown);
    assert(view.grants == ClaudeDetailGrantsState::Unknown);
}
}

int main()
{
    ClaudeDetailSnapshot data = snapshot();
    ClaudeDetailView view = claude_detail_make_view(data, false, 0, 0);
    all_unknown(view);
    assert(!view.age_known);

    view = claude_detail_make_view(data, true, 1000, 2000);
    assert(view.age_known && view.age_seconds == 29);
    assert(view.fable == ClaudeDetailFableState::Value && view.fable_remaining_percent == 88);
    assert(view.fable_resets_at == 123456);
    assert(view.spend == ClaudeDetailSpendState::Value);
    assert(view.currency[0] == 'U' && view.currency[1] == 'S' && view.currency[2] == 'D');
    assert(view.exponent == 2 && view.spend_used == 786 && view.spend_limit == 4000);
    assert(view.grants == ClaudeDetailGrantsState::Value && view.grant_count == 2 && !view.more);
    assert(view.grant_rows[0].type == ClaudeDetailGrantType::Full);
    assert(view.grant_rows[0].resets_left == 1 && view.grant_rows[0].ends_at == 1000);
    assert(view.grant_rows[1].type == ClaudeDetailGrantType::Fable);
    assert(view.grant_rows[1].resets_left == 2 && view.grant_rows[1].ends_at == 2000);

    data.flags |= ClaudeDetailMoreGrants;
    assert(claude_detail_make_view(data, true, 0, 0).more);
    data.fable_used_bp = 1;
    assert(claude_detail_make_view(data, true, 0, 0).fable_remaining_percent == 99);
    data.fable_used_bp = 10000;
    assert(claude_detail_make_view(data, true, 0, 0).fable_remaining_percent == 0);
    data.fable_used_bp = 0;
    assert(claude_detail_make_view(data, true, 0, 0).fable_remaining_percent == 100);
    data.spend_limit = 0;
    view = claude_detail_make_view(data, true, 0, 0);
    assert(view.spend == ClaudeDetailSpendState::Value && view.spend_limit == 0);

    data = snapshot();
    data.flags = ClaudeDetailFableKnown | ClaudeDetailSpendKnown | ClaudeDetailGrantsKnown;
    data.grant_count = 0;
    view = claude_detail_make_view(data, true, 0, 0);
    assert(view.fable == ClaudeDetailFableState::NoPlan);
    assert(view.spend == ClaudeDetailSpendState::Disabled);
    assert(view.grants == ClaudeDetailGrantsState::None && !view.more);
    data.flags = ClaudeDetailFablePresent | ClaudeDetailSpendEnabled |
                 ClaudeDetailAmountValid | ClaudeDetailMoreGrants;
    all_unknown(claude_detail_make_view(data, true, 0, 0));

    data = snapshot();
    data.flags &= static_cast<uint8_t>(~ClaudeDetailAmountValid);
    assert(claude_detail_make_view(data, true, 0, 0).spend == ClaudeDetailSpendState::Unknown);
    data.flags |= ClaudeDetailAmountValid;
    data.exponent = 255;
    assert(claude_detail_make_view(data, true, 0, 0).spend == ClaudeDetailSpendState::Unknown);
    data.exponent = 2;
    data.currency[0] = data.currency[1] = data.currency[2] = 0;
    assert(claude_detail_make_view(data, true, 0, 0).spend == ClaudeDetailSpendState::Unknown);
    data.currency[0] = 'E';
    data.currency[1] = 'U';
    data.currency[2] = 'R';
    view = claude_detail_make_view(data, true, 0, 0);
    assert(view.spend == ClaudeDetailSpendState::Value && view.currency[0] == 'E');

    data = snapshot();
    data.sent_at = data.captured_at;
    view = claude_detail_make_view(data, true, 1000, 3601000);
    assert(view.age_known && view.age_seconds == 3600 && view.fable == ClaudeDetailFableState::Value);
    view = claude_detail_make_view(data, true, 1000, 3602000);
    assert(view.age_known && view.age_seconds == 3601);
    all_unknown(view);
    view = claude_detail_make_view(data, true, UINT32_MAX - 499U, 1500);
    assert(view.age_known && view.age_seconds == 2 && view.fable == ClaudeDetailFableState::Value);
    view = claude_detail_make_view(data, true, 1000, 1999);
    assert(view.age_known && view.age_seconds == 0);

    data.sent_at = data.captured_at - 1;
    view = claude_detail_make_view(data, true, 0, 0);
    assert(!view.age_known);
    all_unknown(view);
    data.captured_at = 0;
    view = claude_detail_make_view(data, true, 0, 0);
    assert(!view.age_known);
    all_unknown(view);

    for (const uint8_t source : {uint8_t{2}, uint8_t{3}}) {
        data = snapshot();
        data.source_state = source;
        data.sent_at += 4000;
        view = claude_detail_make_view(data, true, 0, 0);
        assert(view.age_known && view.age_seconds > 3600);
        assert(view.fable == ClaudeDetailFableState::NotLoggedIn);
        assert(view.spend == ClaudeDetailSpendState::NotLoggedIn);
        assert(view.grants == ClaudeDetailGrantsState::NotLoggedIn);
        data.captured_at = 0;
        view = claude_detail_make_view(data, true, 0, 0);
        assert(!view.age_known && view.fable == ClaudeDetailFableState::NotLoggedIn);
        all_unknown(claude_detail_make_view(data, false, 0, 0));
    }

    data = snapshot();
    data.captured_at = 1;
    data.sent_at = UINT64_MAX;
    view = claude_detail_make_view(data, true, 0, 2000);
    assert(view.age_known && view.age_seconds == UINT64_MAX);
    all_unknown(view);
    return 0;
}
