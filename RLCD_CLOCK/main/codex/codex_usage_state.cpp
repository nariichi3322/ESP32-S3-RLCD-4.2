// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#include "codex_usage_state.h"

#include "scoped_semaphore_lock.h"

namespace {
StaticTaskMutex s_mutex;
CodexUsageSnapshotView s_view;
// Claude reuses the Codex v3 payload on its own characteristic, so it keeps a
// separate snapshot but shares the BLE link lifecycle.
CodexUsageSnapshotView s_claude_view;

CodexUsageParseResult submit_to(CodexUsageSnapshotView &view,
                                const char *payload,
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
        codex_usage_state_commit(view, parsed, now_tick_ms);
    if (commit == CodexUsageCommitResult::SequenceRollback) {
        return CodexUsageParseResult::OutOfRange;
    }
    if (display_changed) {
        *display_changed = commit == CodexUsageCommitResult::AcceptedChanged;
    }
    return result;
}

bool copy_from(const CodexUsageSnapshotView &view, CodexUsageSnapshotView *out)
{
    if (!out) return false;
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return false;
    *out = view;
    return true;
}
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
    codex_usage_state_clear(s_claude_view);
}

void codex_usage_state_connection_changed(bool connected, bool bonded)
{
    ScopedSemaphoreLock lock(s_mutex.handle());
    if (!lock) return;
    codex_usage_state_set_connection(s_view, connected, bonded);
    codex_usage_state_set_connection(s_claude_view, connected, bonded);
}

CodexUsageParseResult codex_usage_state_submit(const char *payload,
                                               size_t payload_size,
                                               uint32_t now_tick_ms,
                                               bool *display_changed)
{
    return submit_to(s_view, payload, payload_size, now_tick_ms, display_changed);
}

CodexUsageParseResult claude_usage_state_submit(const char *payload,
                                                size_t payload_size,
                                                uint32_t now_tick_ms,
                                                bool *display_changed)
{
    return submit_to(s_claude_view, payload, payload_size, now_tick_ms,
                     display_changed);
}

bool codex_usage_snapshot_copy(CodexUsageSnapshotView *out)
{
    return copy_from(s_view, out);
}

bool claude_usage_snapshot_copy(CodexUsageSnapshotView *out)
{
    return copy_from(s_claude_view, out);
}
