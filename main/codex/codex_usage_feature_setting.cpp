// Persists and applies the CODEX Usage Display feature switch atomically.
#include "device_settings_persistence.h"

#include "codex_usage_ble.h"
#include "codex_usage_feature_state_internal.h"
#include "network_config_keys.h"
#include "network_config_nvs.h"

namespace {
constexpr const char *kNvsActionSavingCodexUsageFeature =
    "saving Codex Usage feature";
}

bool set_codex_usage_feature_setting(bool enabled)
{
    network_config_nvs::ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE,
                             kNvsActionSavingCodexUsageFeature);
    if (err != ESP_OK) return false;

    bool changed = false;
    err = network_config_nvs::write_changed_nvs_u8(
        nvs.get(),
        err,
        network_config_keys::kCodexUsageFeatureKey,
        enabled ? 1U : 0U,
        &changed);
    err = network_config_nvs::commit_nvs_if_changed(
        nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) return false;

    codex_usage_feature_enabled_store(enabled);
    return codex_usage_ble_set_enabled(enabled);
}
