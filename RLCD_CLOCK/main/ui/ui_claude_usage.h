#pragma once

#include "codex_usage_state.h"

#include <time.h>

// Claude usage reuses the Codex v3 snapshot; a zero window means unavailable.
void build_claude_usage_page();
bool update_claude_usage_page(const struct tm &local,
                              const CodexUsageSnapshotView &view,
                              bool feature_enabled);
void clear_claude_usage_page_object_refs();
