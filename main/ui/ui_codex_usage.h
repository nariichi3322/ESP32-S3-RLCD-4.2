#pragma once

#include "codex_usage_state.h"

#include <time.h>

void build_codex_usage_page();
bool update_codex_usage_page(const struct tm &local,
                             const CodexUsageSnapshotView &view);
void clear_codex_usage_page_object_refs();
