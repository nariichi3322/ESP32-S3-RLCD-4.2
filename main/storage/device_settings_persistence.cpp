// 负责声音、工作页、小智自动返回和图库周期设置的条件写入与 NVS 提交。
#include "device_settings_persistence.h"

#include "app_metadata.h"
#include "chime_runtime_state.h"
#include "network_chime_storage.h"
#include "network_config_keys.h"
#include "network_config_nvs.h"
#include "network_page_storage.h"
#include "ui_work_page_catalog.h"
#include "ui_gallery_rotation_state.h"
#include "xiaozhi_auto_return_state.h"

#include <esp_log.h>

using network_config_nvs::commit_nvs_if_changed;
using network_config_nvs::ScopedNvsHandle;
using network_config_nvs::write_changed_nvs_u8;
using network_page_storage::kPageMaskV5Key;
using network_page_storage::write_work_page_order_nvs;
using network_config_keys::kXiaozhiAutoReturnKey;
using network_config_keys::kGalleryRotationKey;

namespace {
constexpr const char *kNvsActionSavingHourlyReminder = "saving hourly reminder";
constexpr const char *kNvsActionSavingPageSettings = "saving page settings";
constexpr const char *kNvsActionSavingPageOrder = "saving page order";
constexpr const char *kNvsActionSavingXiaozhiAutoReturn = "saving Xiaozhi auto return";
constexpr const char *kNvsActionSavingGalleryRotation = "saving gallery rotation";
#define NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT "nvs save hourly reminder failed: %s"
#define NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT "nvs save page settings failed: %s"
#define NVS_SAVE_PAGE_ORDER_FAILED_FORMAT "nvs save page order failed: %s"
#define NVS_SAVE_XIAOZHI_AUTO_RETURN_FAILED_FORMAT "nvs save Xiaozhi auto return failed: %s"
#define NVS_SAVE_GALLERY_ROTATION_FAILED_FORMAT "nvs save gallery rotation failed: %s"

constexpr uint8_t bool_to_nvs_u8(bool value)
{
    return value ? 1 : 0;
}
} // namespace

bool save_hourly_chime_setting()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingHourlyReminder);
    if (err != ESP_OK) {
        return false;
    }
    const ChimeRuntimeSnapshot runtime = chime_runtime_snapshot_load();
    network_chime_storage::StoredChimeSettings settings = {};
    settings.enabled = bool_to_nvs_u8(runtime.hourly_enabled);
    settings.all_day = bool_to_nvs_u8(runtime.all_day);
    settings.volume = runtime.volume_percent;
    settings.sound = runtime.sound_index;
    bool changed = false;
    err = network_chime_storage::write_if_changed(nvs.get(), err, settings, &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_HOURLY_REMINDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_work_page_settings()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingPageSettings);
    if (err != ESP_OK) {
        return false;
    }
    uint8_t mask = normalize_work_page_enabled_mask(work_page_enabled_mask_load());
    bool changed = false;
    err = write_changed_nvs_u8(nvs.get(), err, kPageMaskV5Key, mask, &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_SETTINGS_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    work_page_enabled_mask_store(mask);
    return true;
}

bool save_work_page_order()
{
    uint8_t page_order[kWorkPageCount] = {};
    if (!work_page_order_normalize_and_copy(page_order, sizeof(page_order))) {
        return false;
    }
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingPageOrder);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_work_page_order_nvs(nvs.get(),
                                    err,
                                    page_order,
                                    sizeof(page_order),
                                    &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_PAGE_ORDER_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_xiaozhi_auto_return_setting()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingXiaozhiAutoReturn);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_changed_nvs_u8(nvs.get(),
                               err,
                               kXiaozhiAutoReturnKey,
                               bool_to_nvs_u8(xiaozhi_auto_return_enabled_load()),
                               &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_XIAOZHI_AUTO_RETURN_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_gallery_rotation_setting()
{
    ScopedNvsHandle nvs;
    esp_err_t err = nvs.open(NVS_READWRITE, kNvsActionSavingGalleryRotation);
    if (err != ESP_OK) {
        return false;
    }
    bool changed = false;
    err = write_changed_nvs_u8(nvs.get(),
                               err,
                               kGalleryRotationKey,
                               gallery_rotation_period_load(),
                               &changed);
    err = commit_nvs_if_changed(nvs.get(), err, changed);
    if (!nvs.close_save_ok(err)) {
        ESP_LOGW(TAG, NVS_SAVE_GALLERY_ROTATION_FAILED_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}
