// 持久化小智绑定结果和设备 Client ID，不参与网络与语音状态机。
#include "xiaozhi_activation_storage.h"

#include "app_state.h"

#include <cJSON.h>
#include <esp_system.h>
#include <nvs.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr const char *kNvsNamespace = "xiaozhi";
constexpr const char *kWebsocketUrlKey = "ws_url";
constexpr const char *kWebsocketTokenKey = "ws_token";
constexpr const char *kWebsocketVersionKey = "ws_ver";
constexpr const char *kActivationChallengeKey = "act_chal";
constexpr const char *kClientIdKey = "client_id";
constexpr const char *kBindingConfirmedKey = "bound_v1";

#define XIAOZHI_NVS_CLEAR_OPEN_FAILED_FORMAT "xiaozhi activation NVS open for clear failed: %s"
#define XIAOZHI_NVS_CLEAR_ERASE_FAILED_FORMAT "xiaozhi activation NVS erase failed: %s"
#define XIAOZHI_NVS_CLEAR_COMMIT_FAILED_FORMAT "xiaozhi activation NVS clear commit failed: %s"
#define XIAOZHI_ACTIVATION_NVS_OPEN_FAILED_FORMAT "xiaozhi activation NVS open failed: %s"
#define XIAOZHI_ACTIVATION_NVS_SAVE_FAILED_FORMAT "xiaozhi activation NVS save failed: %s"
#define XIAOZHI_CLIENT_ID_NVS_OPEN_FAILED_FORMAT "xiaozhi client id NVS open failed: %s"
#define XIAOZHI_CLIENT_ID_NVS_SAVE_FAILED_FORMAT "xiaozhi client id NVS save failed: %s"

static_assert(kXiaozhiClientIdSize == sizeof("00000000-0000-0000-0000-000000000000"),
              "Xiaozhi client ID buffer must fit one UUID plus terminator");

bool output_buffer_available(const char *out, size_t out_len)
{
    return out && out_len > 0;
}

bool nvs_read_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len)
{
    if (!key || !output_buffer_available(out, out_len)) {
        return false;
    }
    size_t len = out_len;
    if (nvs_get_str(nvs, key, out, &len) != ESP_OK || out[0] == '\0') {
        out[0] = '\0';
        return false;
    }
    return true;
}
} // namespace

bool xiaozhi_load_websocket_config(char *url,
                                    size_t url_len,
                                    char *token,
                                    size_t token_len,
                                    int32_t *version)
{
    if (!output_buffer_available(url, url_len) ||
        !output_buffer_available(token, token_len) || !version) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    uint8_t binding_confirmed = 0;
    bool present = nvs_get_u8(nvs, kBindingConfirmedKey, &binding_confirmed) == ESP_OK &&
                   binding_confirmed == 1 &&
                   nvs_read_string(nvs, kWebsocketUrlKey, url, url_len);
    nvs_read_string(nvs, kWebsocketTokenKey, token, token_len);
    if (nvs_get_i32(nvs, kWebsocketVersionKey, version) != ESP_OK || *version <= 0) {
        *version = 1;
    }
    nvs_close(nvs);
    return present;
}

bool xiaozhi_save_activation_config(cJSON *websocket, const char *challenge)
{
    if (!cJSON_IsObject(websocket)) {
        return false;
    }
    cJSON *url = cJSON_GetObjectItem(websocket, "url");
    cJSON *token = cJSON_GetObjectItem(websocket, "token");
    cJSON *version = cJSON_GetObjectItem(websocket, "version");
    if (!cJSON_IsString(url) || !url->valuestring || url->valuestring[0] == '\0') {
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_NVS_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(nvs, kWebsocketUrlKey, url->valuestring);
    if (err == ESP_OK && cJSON_IsString(token) && token->valuestring) {
        err = nvs_set_str(nvs, kWebsocketTokenKey, token->valuestring);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, kWebsocketVersionKey, cJSON_IsNumber(version) ? version->valueint : 1);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kBindingConfirmedKey, 1);
    }
    if (err == ESP_OK && challenge && challenge[0] != '\0') {
        err = nvs_set_str(nvs, kActivationChallengeKey, challenge);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_ACTIVATION_NVS_SAVE_FAILED_FORMAT, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool xiaozhi_load_or_create_client_id(char *out, size_t out_len)
{
    if (!output_buffer_available(out, out_len) || out_len < kXiaozhiClientIdSize) {
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_CLIENT_ID_NVS_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    size_t stored_len = out_len;
    if (nvs_get_str(nvs, kClientIdKey, out, &stored_len) == ESP_OK &&
        strlen(out) == kXiaozhiClientIdSize - 1) {
        nvs_close(nvs);
        return true;
    }
    uint8_t uuid[16] = {};
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
    snprintf(out,
             out_len,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);
    err = nvs_set_str(nvs, kClientIdKey, out);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_CLIENT_ID_NVS_SAVE_FAILED_FORMAT, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool xiaozhi_clear_activation_storage()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_OPEN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    err = nvs_erase_all(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_ERASE_FAILED_FORMAT, esp_err_to_name(err));
    } else {
        err = nvs_commit(nvs);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, XIAOZHI_NVS_CLEAR_COMMIT_FAILED_FORMAT, esp_err_to_name(err));
        }
    }
    nvs_close(nvs);
    return err == ESP_OK;
}
