#include "codex_usage_feature_state_internal.h"

#include <atomic>

namespace {
std::atomic<bool> s_codex_usage_feature_enabled{
    kDefaultCodexUsageFeatureEnabled};
}

bool codex_usage_feature_enabled()
{
    return s_codex_usage_feature_enabled.load(std::memory_order_acquire);
}

void codex_usage_feature_enabled_store(bool enabled)
{
    s_codex_usage_feature_enabled.store(enabled, std::memory_order_release);
}
