#include "codex_usage_feature_state.h"
#include "codex_usage_feature_state_internal.h"

#include <assert.h>

int main()
{
    static_assert(!kDefaultCodexUsageFeatureEnabled,
                  "Codex Usage feature must default off");
    static_assert(!normalize_codex_usage_feature_setting(0));
    static_assert(normalize_codex_usage_feature_setting(1));
    static_assert(!normalize_codex_usage_feature_setting(2));
    static_assert(!normalize_codex_usage_feature_setting(255));

    assert(!codex_usage_feature_enabled());
    codex_usage_feature_enabled_store(true);
    assert(codex_usage_feature_enabled());
    codex_usage_feature_enabled_store(false);
    assert(!codex_usage_feature_enabled());
    return 0;
}
