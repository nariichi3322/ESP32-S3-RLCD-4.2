// 验证工作页顺序 NVS 条件写入及失败时的变化标记。
#include "network_page_storage.h"
#include "network_page_storage_policy.h"

#include "work_page_ids.h"

#include <assert.h>
#include <string.h>

namespace {
uint8_t g_saved_order[kWorkPageCount] = {};
bool g_have_saved_order = false;
uint8_t g_legacy_order[network_page_storage::kLegacyV4WorkPageCount] = {};
bool g_have_legacy_order = false;
uint8_t g_v5_order[network_page_storage::kLegacyV5WorkPageCount] = {};
bool g_have_v5_order = false;
bool g_have_v6_mask = false;
bool g_have_v5_mask = false;
bool g_have_v4_mask = false;
uint8_t g_v6_mask = 0;
uint8_t g_v5_mask = 0;
uint8_t g_v4_mask = 0;
esp_err_t g_set_blob_result = ESP_OK;
int g_get_blob_calls = 0;
int g_set_blob_calls = 0;

void reset_store()
{
    memset(g_saved_order, 0, sizeof(g_saved_order));
    g_have_saved_order = false;
    memset(g_legacy_order, 0, sizeof(g_legacy_order));
    g_have_legacy_order = false;
    memset(g_v5_order, 0, sizeof(g_v5_order));
    g_have_v5_order = false;
    g_have_v6_mask = g_have_v5_mask = g_have_v4_mask = false;
    g_set_blob_result = ESP_OK;
    g_get_blob_calls = 0;
    g_set_blob_calls = 0;
}
} // namespace

uint8_t normalize_work_page_enabled_mask(uint8_t page_mask)
{
    return page_mask;
}

esp_err_t nvs_get_u8(nvs_handle_t, const char *key, uint8_t *out)
{
    if (!key || !out) return ESP_ERR_INVALID_ARG;
    if (strcmp(key, network_page_storage::kPageMaskV6Key) == 0 && g_have_v6_mask) {
        *out = g_v6_mask;
        return ESP_OK;
    }
    if (strcmp(key, network_page_storage::kPageMaskV5Key) == 0 && g_have_v5_mask) {
        *out = g_v5_mask;
        return ESP_OK;
    }
    if (strcmp(key, network_page_storage::kPageMaskV4Key) == 0 && g_have_v4_mask) {
        *out = g_v4_mask;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *len)
{
    ++g_get_blob_calls;
    if (!key || !out || !len) {
        return ESP_FAIL;
    }
    if (strcmp(key, network_page_storage::kPageOrderV6Key) == 0) {
        if (!g_have_saved_order) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        if (*len < sizeof(g_saved_order)) {
            return ESP_FAIL;
        }
        memcpy(out, g_saved_order, sizeof(g_saved_order));
        *len = sizeof(g_saved_order);
        return ESP_OK;
    }
    if (strcmp(key, network_page_storage::kPageOrderV4Key) == 0) {
        if (!g_have_legacy_order) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        if (*len < sizeof(g_legacy_order)) {
            return ESP_FAIL;
        }
        memcpy(out, g_legacy_order, sizeof(g_legacy_order));
        *len = sizeof(g_legacy_order);
        return ESP_OK;
    }
    if (strcmp(key, network_page_storage::kPageOrderV5Key) == 0) {
        if (!g_have_v5_order) return ESP_ERR_NVS_NOT_FOUND;
        if (*len < sizeof(g_v5_order)) return ESP_FAIL;
        memcpy(out, g_v5_order, sizeof(g_v5_order));
        *len = sizeof(g_v5_order);
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *value, size_t len)
{
    ++g_set_blob_calls;
    if (g_set_blob_result != ESP_OK) {
        return g_set_blob_result;
    }
    if (!key ||
        strcmp(key, network_page_storage::kPageOrderV6Key) != 0 ||
        !value ||
        len != sizeof(g_saved_order)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(g_saved_order, value, len);
    g_have_saved_order = true;
    return ESP_OK;
}

int main()
{
    constexpr nvs_handle_t kNvs = 1;
    const uint8_t order[kWorkPageCount] = {0, 1, 2, 3, 4, 5, 6, 7};

    reset_store();
    assert(network_page_storage::read_saved_page_mask(kNvs) == 0xff);
    g_have_v4_mask = true;
    g_v4_mask = 0x05;
    assert(network_page_storage::read_saved_page_mask(kNvs) == 0xc5);
    g_have_v5_mask = true;
    g_v5_mask = 0x12;
    assert(network_page_storage::read_saved_page_mask(kNvs) == 0x92);
    g_have_v6_mask = true;
    g_v6_mask = 0x24;
    assert(network_page_storage::read_saved_page_mask(kNvs) == 0x24);

    reset_store();
    const uint8_t v5_order[network_page_storage::kLegacyV5WorkPageCount] =
        {6, 2, 0, 1, 3, 4, 5};
    memcpy(g_v5_order, v5_order, sizeof(v5_order));
    g_have_v5_order = true;
    uint8_t migrated_order[kWorkPageCount] = {};
    assert(network_page_storage::read_saved_page_order(
        kNvs, migrated_order, sizeof(migrated_order)));
    assert(memcmp(migrated_order, v5_order, sizeof(v5_order)) == 0);
    assert(migrated_order[kWorkPageCodexUsage] == kWorkPageCodexUsage);

    reset_store();
    bool changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_FAIL, order, sizeof(order), &changed) == ESP_FAIL);
    assert(!changed);
    assert(g_get_blob_calls == 0);
    assert(g_set_blob_calls == 0);

    memcpy(g_saved_order, order, sizeof(order));
    g_have_saved_order = true;
    changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, order, sizeof(order), &changed) == ESP_OK);
    assert(!changed);
    assert(g_get_blob_calls == 1);
    assert(g_set_blob_calls == 0);

    reset_store();
    memcpy(g_legacy_order, order, sizeof(g_legacy_order));
    g_have_legacy_order = true;
    changed = false;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, order, sizeof(order), &changed) == ESP_OK);
    assert(changed);
    assert(g_have_saved_order);
    assert(g_set_blob_calls == 1);
    assert(memcmp(g_saved_order, order, sizeof(order)) == 0);

    reset_store();
    memcpy(g_saved_order, order, sizeof(order));
    g_have_saved_order = true;
    uint8_t changed_order[kWorkPageCount] = {1, 0, 2, 3, 4, 5, 6, 7};
    g_set_blob_result = ESP_FAIL;
    changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, changed_order, sizeof(changed_order), &changed) == ESP_FAIL);
    assert(!changed);
    assert(g_set_blob_calls == 1);

    g_set_blob_result = ESP_OK;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, changed_order, sizeof(changed_order), &changed) == ESP_OK);
    assert(changed);
    assert(g_set_blob_calls == 2);
    assert(memcmp(g_saved_order, changed_order, sizeof(changed_order)) == 0);
    return 0;
}
