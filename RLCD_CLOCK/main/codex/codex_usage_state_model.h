// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#pragma once

#include "codex_usage_protocol.h"

struct CodexUsageStateData {
    CodexUsageSnapshot snapshot{};
    uint32_t last_valid_tick_ms = 0;
    uint32_t received_tick_ms = 0;
    uint32_t generation = 0;
    uint32_t last_sequence = 0;
    bool snapshot_valid = false;
    bool data_valid = false;
    bool ble_connected = false;
    bool bonded = false;
};

enum class CodexUsageCommitResult : uint8_t {
    AcceptedChanged,
    AcceptedUnchanged,
    Heartbeat,
    SequenceRollback,
};

inline bool codex_usage_state_set_connection(CodexUsageStateData &state,
                                             bool connected,
                                             bool bonded)
{
    const bool changed = state.ble_connected != connected ||
                         state.bonded != bonded;
    state.ble_connected = connected;
    state.bonded = bonded;
    if (!connected || !bonded) {
        // A reconnect or a new security session must receive a fresh payload
        // before it can be LINKED.  Keep the snapshot storage intact, but do
        // not treat data from the previous BLE session as current.
        state.data_valid = false;
        state.last_sequence = 0;
    }
    if (changed) ++state.generation;
    return changed;
}

inline CodexUsageCommitResult codex_usage_state_commit(
    CodexUsageStateData &state,
    const CodexUsageSnapshot &snapshot,
    uint32_t now_tick_ms)
{
    if (snapshot.sequence < state.last_sequence) {
        return CodexUsageCommitResult::SequenceRollback;
    }
    state.last_valid_tick_ms = now_tick_ms;
    if (snapshot.sequence == state.last_sequence && state.last_sequence != 0) {
        return CodexUsageCommitResult::Heartbeat;
    }
    const bool changed = !state.snapshot_valid || !state.data_valid ||
                         !codex_usage_display_values_equal(snapshot,
                                                           state.snapshot);
    state.snapshot = snapshot;
    state.received_tick_ms = now_tick_ms;
    state.snapshot_valid = true;
    state.data_valid = true;
    state.last_sequence = snapshot.sequence;
    if (changed) ++state.generation;
    return changed ? CodexUsageCommitResult::AcceptedChanged
                   : CodexUsageCommitResult::AcceptedUnchanged;
}

inline void codex_usage_state_clear(CodexUsageStateData &state)
{
    const uint32_t generation = state.generation + 1U;
    state = {};
    state.generation = generation;
}
