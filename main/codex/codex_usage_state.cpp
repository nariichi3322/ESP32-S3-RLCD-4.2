// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#include "codex_usage_state.h"

#include "scoped_semaphore_lock.h"

namespace {
StaticTaskMutex s_mutex;
CodexUsageSnapshotView s_view;
}

bool codex_usage_state_init()
{
    return s_mutex.init();
}

void codex_usage_state_reset()
{
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return;
    codex_usage_state_clear(s_view);
}

void codex_usage_state_connection_changed(bool connected, bool bonded)
{
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return;
    codex_usage_state_set_connection(s_view, connected, bonded);
}

CodexUsageParseResult codex_usage_state_submit(const char *payload,
                                               size_t payload_size,
                                               uint32_t now_tick_ms,
                                               bool *display_changed)
{
    if (display_changed) *display_changed = false;
    CodexUsageSnapshot parsed{};
    const CodexUsageParseResult result =
        codex_usage_parse_status(payload, payload_size, &parsed);
    if (result != CodexUsageParseResult::Ok) return result;
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return CodexUsageParseResult::Malformed;
    const CodexUsageCommitResult commit =
        codex_usage_state_commit(s_view, parsed, now_tick_ms);
    if (commit == CodexUsageCommitResult::SequenceRollback) {
        return CodexUsageParseResult::OutOfRange;
    }
    const bool changed = commit == CodexUsageCommitResult::AcceptedChanged;
    if (display_changed) *display_changed = changed;
    return result;
}

bool codex_usage_snapshot_copy(CodexUsageSnapshotView *out)
{
    if (!out) return false;
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return false;
    *out = s_view;
    return true;
}
