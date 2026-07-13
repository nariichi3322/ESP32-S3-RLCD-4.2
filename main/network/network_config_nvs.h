// 提供联网配置模块内部复用的 NVS 打开、比较、条件写入和提交原语。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nvs.h"

namespace network_config_nvs {

esp_err_t open_wifi_nvs(nvs_open_mode_t mode,
                        nvs_handle_t *nvs,
                        const char *action,
                        bool log_not_found = true);

class ScopedNvsHandle {
public:
    ScopedNvsHandle() = default;
    ~ScopedNvsHandle() { close(); }

    ScopedNvsHandle(const ScopedNvsHandle &) = delete;
    ScopedNvsHandle &operator=(const ScopedNvsHandle &) = delete;

    esp_err_t open(nvs_open_mode_t mode,
                   const char *action,
                   bool log_not_found = true)
    {
        close();
        esp_err_t err = open_wifi_nvs(mode, &handle_, action, log_not_found);
        open_ = err == ESP_OK;
        return err;
    }

    nvs_handle_t get() const { return handle_; }

    void close()
    {
        if (!open_) {
            return;
        }
        nvs_close(handle_);
        open_ = false;
    }

    bool close_save_ok(esp_err_t err)
    {
        close();
        return err == ESP_OK;
    }

private:
    nvs_handle_t handle_ = 0;
    bool open_ = false;
};

esp_err_t erase_nvs_key_if_present(nvs_handle_t nvs, const char *key, bool *erased);
esp_err_t commit_nvs_if_ok(nvs_handle_t nvs, esp_err_t err);
esp_err_t commit_nvs_if_changed(nvs_handle_t nvs, esp_err_t err, bool changed);
esp_err_t set_nvs_str_if_ok(nvs_handle_t nvs,
                            esp_err_t err,
                            const char *key,
                            const char *value);
esp_err_t set_nvs_u8_if_ok(nvs_handle_t nvs,
                           esp_err_t err,
                           const char *key,
                           uint8_t value);
esp_err_t write_optional_nvs_string_key(nvs_handle_t nvs, const char *key, const char *value);
esp_err_t read_nvs_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len);
bool nvs_string_matches(nvs_handle_t nvs,
                        const char *key,
                        const char *expected,
                        char *scratch,
                        size_t scratch_len);
uint8_t read_nvs_u8_or_default(nvs_handle_t nvs, const char *key, uint8_t default_value);
bool nvs_u8_matches(nvs_handle_t nvs, const char *key, uint8_t expected);
esp_err_t write_changed_nvs_u8(nvs_handle_t nvs,
                               esp_err_t err,
                               const char *key,
                               uint8_t value,
                               bool *changed);
bool close_nvs_save_ok(nvs_handle_t nvs, esp_err_t err);

} // namespace network_config_nvs
