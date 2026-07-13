// 实现闹钟 NVS 三键记录的读取、提交和整命名空间清除。
#include "alarm_storage.h"

#include "nvs.h"

namespace alarm_storage {
namespace {
constexpr const char *kNamespace = "alarm_v1";
constexpr const char *kEnabledKey = "enabled";
constexpr const char *kHourKey = "hour";
constexpr const char *kMinuteKey = "minute";

bool all_keys_missing(esp_err_t enabled_err, esp_err_t hour_err, esp_err_t minute_err)
{
    return enabled_err == ESP_ERR_NVS_NOT_FOUND &&
           hour_err == ESP_ERR_NVS_NOT_FOUND &&
           minute_err == ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t first_read_error(esp_err_t enabled_err, esp_err_t hour_err, esp_err_t minute_err)
{
    if (enabled_err != ESP_OK) {
        return enabled_err;
    }
    if (hour_err != ESP_OK) {
        return hour_err;
    }
    return minute_err;
}
} // namespace

ReadResult read()
{
    ReadResult result = {};
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        result.status = ReadStatus::kEmpty;
        return result;
    }
    if (err != ESP_OK) {
        result.status = ReadStatus::kOpenFailed;
        result.error = err;
        return result;
    }

    esp_err_t enabled_err = nvs_get_u8(nvs, kEnabledKey, &result.enabled);
    esp_err_t hour_err = nvs_get_u8(nvs, kHourKey, &result.hour);
    esp_err_t minute_err = nvs_get_u8(nvs, kMinuteKey, &result.minute);
    nvs_close(nvs);

    if (enabled_err == ESP_OK && hour_err == ESP_OK && minute_err == ESP_OK) {
        result.status = ReadStatus::kLoaded;
    } else if (all_keys_missing(enabled_err, hour_err, minute_err)) {
        result.status = ReadStatus::kEmpty;
    } else {
        result.status = ReadStatus::kIncomplete;
        result.error = first_read_error(enabled_err, hour_err, minute_err);
    }
    return result;
}

WriteResult write(bool enabled, uint8_t hour, uint8_t minute)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return {WriteStatus::kOpenFailed, err};
    }

    err = nvs_set_u8(nvs, kEnabledKey, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kHourKey, hour);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, kMinuteKey, minute);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err == ESP_OK
               ? WriteResult{WriteStatus::kSaved, ESP_OK}
               : WriteResult{WriteStatus::kWriteFailed, err};
}

ClearResult clear()
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return {ClearStatus::kAlreadyEmpty, ESP_OK};
    }
    if (err != ESP_OK) {
        return {ClearStatus::kOpenFailed, err};
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err == ESP_OK
               ? ClearResult{ClearStatus::kCleared, ESP_OK}
               : ClearResult{ClearStatus::kEraseFailed, err};
}

} // namespace alarm_storage
