#include "codex_usage_feature_state.h"
#include "codex_usage_feature_state_internal.h"
#include "device_settings_persistence.h"
#include "network_config_nvs.h"

#include <assert.h>
#include <string.h>

namespace {
constexpr nvs_handle_t kHandle = 1;
bool s_have_value = false;
uint8_t s_value = 0;
bool s_set_fails = false;
bool s_ble_result = true;
int s_set_calls = 0;
int s_commit_calls = 0;
int s_ble_calls = 0;
bool s_last_ble_enabled = false;

void reset_mocks()
{
    s_have_value = false;
    s_value = 0;
    s_set_fails = false;
    s_ble_result = true;
    s_set_calls = 0;
    s_commit_calls = 0;
    s_ble_calls = 0;
    s_last_ble_enabled = false;
    codex_usage_feature_enabled_store(false);
}
} // namespace

namespace network_config_nvs {
esp_err_t open_wifi_nvs(nvs_open_mode_t,
                        nvs_handle_t *out,
                        const char *,
                        bool)
{
    assert(out);
    *out = kHandle;
    return ESP_OK;
}

esp_err_t write_changed_nvs_u8(nvs_handle_t handle,
                               esp_err_t err,
                               const char *key,
                               uint8_t value,
                               bool *changed)
{
    assert(handle == kHandle && err == ESP_OK && key && changed);
    assert(strcmp(key, "codex_ble_v1") == 0);
    if (s_have_value && s_value == value) {
        *changed = false;
        return ESP_OK;
    }
    if (s_set_fails) {
        *changed = false;
        return ESP_FAIL;
    }
    ++s_set_calls;
    s_have_value = true;
    s_value = value;
    *changed = true;
    return ESP_OK;
}

esp_err_t commit_nvs_if_changed(nvs_handle_t handle,
                                esp_err_t err,
                                bool changed)
{
    assert(handle == kHandle);
    if (err == ESP_OK && changed) ++s_commit_calls;
    return err;
}
} // namespace network_config_nvs

void nvs_close(nvs_handle_t handle) { assert(handle == kHandle); }

bool codex_usage_ble_set_enabled(bool enabled)
{
    ++s_ble_calls;
    s_last_ble_enabled = enabled;
    return s_ble_result;
}

int main()
{
    reset_mocks();
    assert(set_codex_usage_feature_setting(true));
    assert(s_have_value && s_value == 1);
    assert(s_set_calls == 1 && s_commit_calls == 1);
    assert(s_ble_calls == 1 && s_last_ble_enabled);
    assert(codex_usage_feature_enabled());

    s_set_fails = true;
    assert(!set_codex_usage_feature_setting(false));
    assert(codex_usage_feature_enabled());
    assert(s_ble_calls == 1);

    s_set_fails = false;
    s_ble_result = false;
    assert(!set_codex_usage_feature_setting(false));
    assert(s_value == 0);
    assert(!codex_usage_feature_enabled());
    assert(s_ble_calls == 2 && !s_last_ble_enabled);

    s_ble_result = true;
    const int commits_before = s_commit_calls;
    assert(set_codex_usage_feature_setting(false));
    assert(s_commit_calls == commits_before);
    assert(s_ble_calls == 3);
    return 0;
}
