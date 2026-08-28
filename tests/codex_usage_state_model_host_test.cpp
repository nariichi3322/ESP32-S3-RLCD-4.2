#include "codex_usage_state_model.h"

#include <assert.h>

int main()
{
    CodexUsageStateData state{};
    assert(codex_usage_state_set_connection(state, true, true));
    assert(state.generation == 1 && state.ble_connected && state.bonded);

    CodexUsageSnapshot first{};
    first.sequence = 4;
    first.remaining_percent = 68;
    first.quota_reset_seconds = 120;
    assert(codex_usage_state_commit(state, first, 1000) ==
           CodexUsageCommitResult::AcceptedChanged);
    assert(state.generation == 2 && state.snapshot_valid && state.data_valid);

    assert(codex_usage_state_commit(state, first, 16000) ==
           CodexUsageCommitResult::Heartbeat);
    assert(state.generation == 2);
    assert(state.last_valid_tick_ms == 16000);
    assert(state.received_tick_ms == 1000);

    CodexUsageSnapshot rollback = first;
    rollback.sequence = 3;
    rollback.remaining_percent = 10;
    assert(codex_usage_state_commit(state, rollback, 17000) ==
           CodexUsageCommitResult::SequenceRollback);
    assert(state.snapshot.remaining_percent == 68);
    assert(state.last_valid_tick_ms == 16000);

    CodexUsageSnapshot same_display = first;
    same_display.sequence = 5;
    assert(codex_usage_state_commit(state, same_display, 61000) ==
           CodexUsageCommitResult::AcceptedUnchanged);
    assert(state.generation == 2 && state.received_tick_ms == 61000);

    CodexUsageSnapshot changed = same_display;
    changed.sequence = 6;
    changed.remaining_percent = 67;
    assert(codex_usage_state_commit(state, changed, 62000) ==
           CodexUsageCommitResult::AcceptedChanged);
    assert(state.generation == 3);

    assert(codex_usage_state_set_connection(state, false, false));
    assert(state.last_sequence == 0 && state.snapshot_valid && !state.data_valid);
    assert(codex_usage_state_set_connection(state, true, true));
    assert(state.snapshot_valid && !state.data_valid);
    CodexUsageSnapshot reconnect = changed;
    reconnect.sequence = 1;
    assert(codex_usage_state_commit(state, reconnect, 63000) ==
           CodexUsageCommitResult::AcceptedChanged);
    assert(state.snapshot_valid && state.data_valid);

    const uint32_t before_clear = state.generation;
    codex_usage_state_clear(state);
    assert(!state.snapshot_valid && !state.data_valid &&
           state.generation == before_clear + 1U);
    return 0;
}
