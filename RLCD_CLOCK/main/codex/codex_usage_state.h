// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#pragma once

#include "codex_usage_protocol.h"
#include "codex_usage_state_model.h"

using CodexUsageSnapshotView = CodexUsageStateData;

bool codex_usage_state_init();
void codex_usage_state_reset();
void codex_usage_state_connection_changed(bool connected, bool bonded);
CodexUsageParseResult codex_usage_state_submit(const char *payload,
                                               size_t payload_size,
                                               uint32_t now_tick_ms,
                                               bool *display_changed = nullptr);
bool codex_usage_snapshot_copy(CodexUsageSnapshotView *out);
