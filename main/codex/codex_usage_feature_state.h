// Declares the persisted CODEX Usage Display feature switch runtime state.
#pragma once

#include <stdint.h>

inline constexpr bool kDefaultCodexUsageFeatureEnabled = false;

constexpr bool normalize_codex_usage_feature_setting(uint8_t value)
{
    return value == 1U;
}

bool codex_usage_feature_enabled();
