// 处理固件更新检查、下载、校验、写入和重启提示流程。
#include "ota_services.h"

#include "network_services.h"
#include "sensor_services.h"
#include "ui_views.h"

#include "custom_assets.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "display_bsp.h"

struct OtaManifest {
    char version[kOtaVersionLen] = {};
    char url[kOtaUrlLen] = {};
    char sha256[kOtaSha256Len] = {};
    char notes[kOtaNotesLen] = {};
    int size = 0;
};

struct OtaCrashBreadcrumb {
    uint32_t magic = 0;
    int phase = 0;
    int total = 0;
    int progress = 0;
};

struct OtaHttpContext {
    char *redirect_url = nullptr;
    size_t redirect_url_len = 0;
};

struct OtaManifestSource {
    const char *name = nullptr;
    const char *url = nullptr;
};

static RTC_DATA_ATTR OtaCrashBreadcrumb s_ota_breadcrumb;
static constexpr uint32_t kOtaBreadcrumbMagic = 0x4f544131;
static constexpr int kOtaMaxRedirects = 5;
static constexpr size_t kOtaRedirectUrlLen = 1024;
static constexpr int kOtaHttpTxBufferSize = 2048;
static constexpr size_t kOtaManifestResponseBufferSize = 2048;
static constexpr int kOtaManifestSourceNameLen = 16;
static constexpr int kSemverComponentCount = 3;
static constexpr size_t kSha256ByteCount = 32;
static constexpr size_t kSha256HexLen = kSha256ByteCount * 2;
static constexpr size_t kOtaDownloadStatusTextLen = 48;
static constexpr int64_t kOtaUsPerMs = 1000;
static constexpr int64_t kOtaUsPerSecond = 1000000;
static constexpr int kOtaBytesPerKiB = 1024;
static constexpr uint32_t kOtaFailureHoldMs = 5000;
static constexpr uint32_t kOtaSuccessHoldMs = 6000;
static constexpr uint32_t kOtaOfflineHoldMs = 3500;
static constexpr uint32_t kOtaRebootNoticeDelayMs = 3500;
static constexpr uint32_t kOtaPreRestartDisplayQuietMs = 1500;
static constexpr uint32_t kOtaWifiConnectTimeoutMs = 45000;
static constexpr uint32_t kOtaReadRetryDelayMs = 100;
static constexpr TickType_t kOtaReadRetryDelay = pdMS_TO_TICKS(kOtaReadRetryDelayMs);
static constexpr int kHttpStatusMovedPermanently = 301;
static constexpr int kHttpStatusFound = 302;
static constexpr int kHttpStatusSeeOther = 303;
static constexpr int kHttpStatusTemporaryRedirect = 307;
static constexpr int kHttpStatusPermanentRedirect = 308;
static_assert(kOtaMaxRedirects > 0, "OTA redirect limit must be positive");
static_assert(kOtaRedirectUrlLen > kOtaUrlLen, "OTA redirect URL buffer must exceed manifest URL storage");
static_assert(kOtaHttpTxBufferSize > 0, "OTA HTTP tx buffer must be positive");
static_assert(kOtaManifestResponseBufferSize > 1, "OTA manifest response buffer must fit text and NUL");
static_assert(kOtaManifestSourceNameLen > 1, "OTA manifest source name must fit text and NUL");
static_assert(kSemverComponentCount == 3, "OTA semantic version comparison expects three components");
static_assert(kSha256ByteCount == 32, "OTA SHA256 byte count must remain 32");
static_assert(kSha256HexLen + 1 == kOtaSha256Len, "OTA SHA256 hex length must match manifest storage");
static_assert(kOtaDownloadStatusTextLen <= kOtaStatusLen,
              "OTA download status scratch text must fit global OTA status storage");
static_assert(kOtaUsPerSecond == kOtaUsPerMs * 1000, "OTA microsecond constants must stay consistent");
static_assert(kOtaBytesPerKiB == 1024, "OTA KiB conversion must remain binary");
static_assert(kOtaFailureHoldMs > 0 && kOtaSuccessHoldMs > 0 && kOtaOfflineHoldMs > 0,
              "OTA terminal status hold times must be positive");
static_assert(kOtaRebootNoticeDelayMs >= kOtaPreRestartDisplayQuietMs,
              "OTA reboot notice must outlast pre-restart display quiet window");
static_assert(kOtaWifiConnectTimeoutMs > 0, "OTA Wi-Fi connect timeout must be positive");
static_assert(kOtaReadRetryDelayMs > 0, "OTA read retry delay must be positive");
static_assert(kOtaReadRetryDelay > 0, "OTA read retry tick delay must be positive");
static_assert(kHttpStatusMovedPermanently < kHttpStatusFound &&
                  kHttpStatusFound < kHttpStatusSeeOther &&
                  kHttpStatusSeeOther < kHttpStatusTemporaryRedirect &&
                  kHttpStatusTemporaryRedirect < kHttpStatusPermanentRedirect,
              "OTA HTTP redirect status constants must stay ordered");
static constexpr const char *kOtaStatusCheckFailed = "Check failed";
static constexpr const char *kOtaStatusCheckingUpdate = "Checking update";
static constexpr const char *kOtaStatusAlreadyLatest = "Already latest";
static constexpr const char *kOtaStatusDownloadFailed = "Download failed";
static constexpr const char *kOtaStatusVerifyFailed = "Verify failed";
static constexpr const char *kOtaStatusUpdateFailed = "Update failed";
static constexpr const char *kOtaStatusUpdateDoneRebooting = "Update done. Rebooting...";
static constexpr const char *kOtaStatusNoWifi = "No WiFi";
static constexpr const char *kOtaStatusLowBattery = "Low battery";
static constexpr const char *kOtaStatusWifiFailed = "WiFi failed";
static constexpr const char *kOtaStatusNoOtaSlot = "No OTA slot";
static constexpr const char *kOtaStatusNoMemory = "No memory";
static constexpr const char *kOtaStatusOfflineMode = "Offline mode";
static constexpr const char *kOtaStatusUnavailable = "Update unavailable";
static constexpr const char *kOtaStatusIdlePrompt = "BOOT: Check Update";
static constexpr const char *kOtaStatusInstallingUpdate = "Installing update 0%";
static constexpr const char *kOtaStatusInstallingBackup = "Installing backup 0%";
static constexpr const char *kOtaStatusInstallingProgressFormat = "Installing %d%%  %dKB/s";
static constexpr const char *kOtaStatusNewVersionFormat = "New version %s";
static constexpr const char *kOtaStatusFallbackError = "OTA status error";
static constexpr const char *kOtaManifestJsonVersionField = "version";
static constexpr const char *kOtaManifestJsonUrlField = "url";
static constexpr const char *kOtaManifestJsonSha256Field = "sha256";
static constexpr const char *kOtaManifestJsonSizeField = "size";
static constexpr const char *kOtaManifestJsonNotesField = "notes";
static constexpr const char *kOtaManifestSourceR2 = "R2";
static constexpr const char *kOtaManifestSourceGithub = "GitHub";
static constexpr const char *kOtaManifestSourceCustom = "Custom";
static constexpr const char *kOtaUnknownManifestSource = "unknown";
static constexpr const char *kOtaPlaceholderManifestHost = "example.invalid";
static constexpr const char *kOtaRequestFallbackName = "request";
static constexpr int kOtaBuiltInManifestSourceCount = 2;
static constexpr int kOtaBackupManifestSourceIndex = 1;
static constexpr OtaManifestSource kOtaBuiltInManifestSources[] = {
    {kOtaManifestSourceR2, kOtaManifestUrl},
    {kOtaManifestSourceGithub, kOtaBackupManifestUrl},
};
#define OTA_TASK_WATCHDOG_SUBSCRIBE_SKIPPED_FORMAT "OTA task watchdog subscribe skipped: %s"
#define OTA_TASK_WATCHDOG_UNSUBSCRIBE_FAILED_FORMAT "OTA task watchdog unsubscribe failed: %s"
#define OTA_REQUEST_EVENT_GROUP_UNAVAILABLE_FORMAT "OTA %s skipped: event group unavailable"
#define OTA_HEAP_DIAGNOSTIC_FORMAT "OTA heap %s: total=%d progress=%d dma_free=%u dma_largest=%u internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u"
static constexpr const char *kOtaAppMarkedValidLog = "OTA app marked valid";
#define OTA_APP_VALID_MARK_FAILED_FORMAT "OTA app valid mark failed: %s"
#define OTA_PREVIOUS_BREADCRUMB_FORMAT "previous OTA breadcrumb: phase=%d total=%d progress=%d%% reset=%d"
static constexpr const char *kOtaManifestParseInvalidArgLog = "OTA manifest parse invalid arg";
static constexpr const char *kOtaManifestJsonParseFailedLog = "OTA manifest JSON parse failed";
#define OTA_MANIFEST_MISSING_REQUIRED_FIELDS_FORMAT "OTA manifest missing required fields version=%d url=%d sha=%d"
#define OTA_MANIFEST_SHA_INVALID_FORMAT "OTA manifest sha invalid len=%u"
#define OTA_MANIFEST_SOURCE_SKIPPED_FORMAT "OTA manifest source skipped: %s"
static constexpr const char *kOtaManifestResponseAllocFailedLog = "OTA manifest response alloc failed";
#define OTA_MANIFEST_FETCH_FAILED_FORMAT "OTA manifest failed source=%s err=%s"
#define OTA_MANIFEST_PARSE_FAILED_FORMAT "OTA manifest parse failed source=%s"
#define OTA_MANIFEST_LOADED_FORMAT "OTA manifest loaded source=%s version=%s"
#define OTA_BACKUP_MANIFEST_MISMATCH_FORMAT "OTA backup manifest mismatch current=%s backup=%s"
static constexpr const char *kOtaManifestInvalidForInstallLog = "OTA manifest invalid for install";
#define OTA_DOWNLOAD_START_FORMAT "OTA start: reset=%d battery=%d%% %.3fV rssi=%d size=%d url=%s"
#define OTA_HTTP_OPEN_FAILED_FORMAT "OTA http open failed: %s"
#define OTA_REDIRECT_STATUS_FORMAT "OTA redirect status=%d location=%s"
#define OTA_HTTP_STATUS_FAILED_FORMAT "OTA http status=%d content_len=%d"
static constexpr const char *kOtaRedirectLimitReachedLog = "OTA redirect limit reached";
#define OTA_BEGIN_FAILED_FORMAT "OTA begin failed: %s"
#define OTA_DOWNLOAD_TIMEOUT_FORMAT "OTA download timed out total=%d"
#define OTA_READ_FAILED_NO_PROGRESS_FORMAT "OTA read failed with no progress total=%d"
#define OTA_STALLED_FORMAT "OTA stalled total=%d"
#define OTA_SHA_MISMATCH_FORMAT "OTA sha mismatch expected=%s actual=%s"
#define OTA_END_FAILED_FORMAT "OTA end failed: %s"
#define OTA_APP_DESCRIPTION_FAILED_FORMAT "OTA app description failed: %s"
#define OTA_IMAGE_READY_FORMAT "OTA image ready: version=%s project=%s"
#define OTA_BOOT_PARTITION_FAILED_FORMAT "OTA boot partition failed: %s"
static constexpr const char *kOtaTaskEventGroupUnavailableLog = "OTA task stopped: event group unavailable";
#define OTA_UPDATE_CHECK_FORMAT "OTA update check source=%s remote=%s current=%s"
static constexpr const char *kOtaPrimaryDownloadRetryBackupLog =
    "OTA primary download failed, retrying GitHub backup";
static constexpr const char *kOtaStatusTexts[] = {
    kOtaStatusCheckFailed,
    kOtaStatusCheckingUpdate,
    kOtaStatusAlreadyLatest,
    kOtaStatusDownloadFailed,
    kOtaStatusVerifyFailed,
    kOtaStatusUpdateFailed,
    kOtaStatusUpdateDoneRebooting,
    kOtaStatusNoWifi,
    kOtaStatusLowBattery,
    kOtaStatusWifiFailed,
    kOtaStatusNoOtaSlot,
    kOtaStatusNoMemory,
    kOtaStatusOfflineMode,
    kOtaStatusUnavailable,
    kOtaStatusIdlePrompt,
    kOtaStatusInstallingUpdate,
    kOtaStatusInstallingBackup,
    kOtaStatusInstallingProgressFormat,
    kOtaStatusNewVersionFormat,
    kOtaStatusFallbackError,
};
static constexpr const char *kOtaManifestTexts[] = {
    kOtaManifestJsonVersionField,
    kOtaManifestJsonUrlField,
    kOtaManifestJsonSha256Field,
    kOtaManifestJsonSizeField,
    kOtaManifestJsonNotesField,
    kOtaManifestSourceR2,
    kOtaManifestSourceGithub,
    kOtaManifestSourceCustom,
    kOtaUnknownManifestSource,
    kOtaPlaceholderManifestHost,
    kOtaRequestFallbackName,
};
static constexpr const char *kOtaLogTexts[] = {
    OTA_TASK_WATCHDOG_SUBSCRIBE_SKIPPED_FORMAT,
    OTA_TASK_WATCHDOG_UNSUBSCRIBE_FAILED_FORMAT,
    OTA_REQUEST_EVENT_GROUP_UNAVAILABLE_FORMAT,
    OTA_HEAP_DIAGNOSTIC_FORMAT,
    kOtaAppMarkedValidLog,
    OTA_APP_VALID_MARK_FAILED_FORMAT,
    OTA_PREVIOUS_BREADCRUMB_FORMAT,
    kOtaManifestParseInvalidArgLog,
    kOtaManifestJsonParseFailedLog,
    OTA_MANIFEST_MISSING_REQUIRED_FIELDS_FORMAT,
    OTA_MANIFEST_SHA_INVALID_FORMAT,
    OTA_MANIFEST_SOURCE_SKIPPED_FORMAT,
    kOtaManifestResponseAllocFailedLog,
    OTA_MANIFEST_FETCH_FAILED_FORMAT,
    OTA_MANIFEST_PARSE_FAILED_FORMAT,
    OTA_MANIFEST_LOADED_FORMAT,
    OTA_BACKUP_MANIFEST_MISMATCH_FORMAT,
    kOtaManifestInvalidForInstallLog,
    OTA_DOWNLOAD_START_FORMAT,
    OTA_HTTP_OPEN_FAILED_FORMAT,
    OTA_REDIRECT_STATUS_FORMAT,
    OTA_HTTP_STATUS_FAILED_FORMAT,
    kOtaRedirectLimitReachedLog,
    OTA_BEGIN_FAILED_FORMAT,
    OTA_DOWNLOAD_TIMEOUT_FORMAT,
    OTA_READ_FAILED_NO_PROGRESS_FORMAT,
    OTA_STALLED_FORMAT,
    OTA_SHA_MISMATCH_FORMAT,
    OTA_END_FAILED_FORMAT,
    OTA_APP_DESCRIPTION_FAILED_FORMAT,
    OTA_IMAGE_READY_FORMAT,
    OTA_BOOT_PARTITION_FAILED_FORMAT,
    kOtaTaskEventGroupUnavailableLog,
    OTA_UPDATE_CHECK_FORMAT,
    kOtaPrimaryDownloadRetryBackupLog,
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr size_t cstr_len(const char *text)
{
    size_t len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

constexpr bool ota_manifest_source_name_fits(const char *text)
{
    return cstr_nonempty(text) && cstr_len(text) < kOtaManifestSourceNameLen;
}

static const char *ota_request_name_or_fallback(const char *name)
{
    return cstr_nonempty(name) ? name : kOtaRequestFallbackName;
}

static const char *ota_status_text_or_fallback(const char *text)
{
    return cstr_nonempty(text) ? text : kOtaStatusFallbackError;
}

static bool ota_output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

static bool ota_format_failed(int written, size_t out_len)
{
    return written < 0 || static_cast<size_t>(written) >= out_len;
}

static void format_ota_status_text(char *out, size_t out_len, const char *fmt, ...)
{
    if (!ota_output_buffer_available(out, out_len)) {
        return;
    }
    if (!fmt) {
        strlcpy(out, kOtaStatusFallbackError, out_len);
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out, out_len, fmt, args);
    va_end(args);
    if (ota_format_failed(written, out_len)) {
        strlcpy(out, kOtaStatusFallbackError, out_len);
    }
}

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&texts)[N])
{
    for (const char *text : texts) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

static_assert(array_count(kOtaStatusTexts) > 0,
              "OTA status text guard must cover status texts");
static_assert(array_count(kOtaManifestTexts) > 0,
              "OTA manifest text guard must cover manifest texts");
static_assert(array_count(kOtaLogTexts) > 0,
              "OTA log text guard must cover diagnostic texts");
static_assert(cstr_array_nonempty(kOtaStatusTexts), "OTA status texts must be non-empty");
static_assert(cstr_array_nonempty(kOtaManifestTexts), "OTA manifest field texts must be non-empty");
static_assert(cstr_array_nonempty(kOtaLogTexts), "OTA diagnostic texts must be non-empty");
static_assert(array_count(kOtaBuiltInManifestSources) == kOtaBuiltInManifestSourceCount,
              "OTA built-in manifest source list must cover R2 and GitHub");
static_assert(kOtaBackupManifestSourceIndex >= 0 &&
                  kOtaBackupManifestSourceIndex < kOtaBuiltInManifestSourceCount,
              "OTA backup manifest source index must stay within built-in source list");
static_assert(ota_manifest_source_name_fits(kOtaManifestSourceR2),
              "R2 OTA manifest source name must fit UI storage");
static_assert(ota_manifest_source_name_fits(kOtaManifestSourceGithub),
              "GitHub OTA manifest source name must fit UI storage");
static_assert(ota_manifest_source_name_fits(kOtaManifestSourceCustom),
              "custom OTA manifest source name must fit UI storage");
static_assert(ota_manifest_source_name_fits(kOtaUnknownManifestSource),
              "unknown OTA manifest source name must fit UI storage");

static void log_ota_heap(const char *stage, int downloaded, int progress)
{
    ESP_LOGI(TAG,
             OTA_HEAP_DIAGNOSTIC_FORMAT,
             stage,
             downloaded,
             progress,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static int ota_speed_kbps_for_window(int bytes, int64_t elapsed_us)
{
    if (bytes <= 0 || elapsed_us <= 0) {
        return 0;
    }
    return (int)((int64_t)bytes * kOtaUsPerSecond / elapsed_us / kOtaBytesPerKiB);
}

class OtaDisplayQuietGuard {
public:
    OtaDisplayQuietGuard()
    {
        Display_SetOtaQuietMode(true);
    }

    ~OtaDisplayQuietGuard()
    {
        Display_SetOtaQuietMode(false);
    }

    OtaDisplayQuietGuard(const OtaDisplayQuietGuard &) = delete;
    OtaDisplayQuietGuard &operator=(const OtaDisplayQuietGuard &) = delete;
};

class OtaTaskWatchdogGuard {
public:
    OtaTaskWatchdogGuard()
    {
        if (esp_task_wdt_status(nullptr) == ESP_OK) {
            active_ = true;
            return;
        }
        esp_err_t err = esp_task_wdt_add(nullptr);
        if (err == ESP_OK) {
            active_ = true;
            added_ = true;
        } else {
            ESP_LOGW(TAG, OTA_TASK_WATCHDOG_SUBSCRIBE_SKIPPED_FORMAT, esp_err_to_name(err));
        }
    }

    ~OtaTaskWatchdogGuard()
    {
        if (added_) {
            esp_err_t err = esp_task_wdt_delete(nullptr);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, OTA_TASK_WATCHDOG_UNSUBSCRIBE_FAILED_FORMAT, esp_err_to_name(err));
            }
        }
    }

    OtaTaskWatchdogGuard(const OtaTaskWatchdogGuard &) = delete;
    OtaTaskWatchdogGuard &operator=(const OtaTaskWatchdogGuard &) = delete;

    void reset()
    {
        if (active_) {
            esp_task_wdt_reset();
        }
    }

private:
    bool active_ = false;
    bool added_ = false;
};

class OtaManifestResponseBuffer {
public:
    explicit OtaManifestResponseBuffer(size_t size)
        : data_((char *)malloc(size)),
          size_(size)
    {
        if (data_) {
            data_[0] = '\0';
        }
    }

    ~OtaManifestResponseBuffer()
    {
        free(data_);
    }

    OtaManifestResponseBuffer(const OtaManifestResponseBuffer &) = delete;
    OtaManifestResponseBuffer &operator=(const OtaManifestResponseBuffer &) = delete;

    char *data() const
    {
        return data_;
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

private:
    char *data_ = nullptr;
    size_t size_ = 0;
};

class OtaJsonRoot {
public:
    explicit OtaJsonRoot(const char *json)
        : root_(cJSON_Parse(json))
    {
    }

    ~OtaJsonRoot()
    {
        cJSON_Delete(root_);
    }

    OtaJsonRoot(const OtaJsonRoot &) = delete;
    OtaJsonRoot &operator=(const OtaJsonRoot &) = delete;

    cJSON *get() const
    {
        return root_;
    }

    explicit operator bool() const
    {
        return root_ != nullptr;
    }

private:
    cJSON *root_ = nullptr;
};

class OtaDownloadBuffer {
public:
    explicit OtaDownloadBuffer(size_t size)
        : data_((uint8_t *)malloc(size)),
          size_(size)
    {
    }

    ~OtaDownloadBuffer()
    {
        free(data_);
    }

    OtaDownloadBuffer(const OtaDownloadBuffer &) = delete;
    OtaDownloadBuffer &operator=(const OtaDownloadBuffer &) = delete;

    uint8_t *data() const
    {
        return data_;
    }

    int size() const
    {
        return (int)size_;
    }

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

private:
    uint8_t *data_ = nullptr;
    size_t size_ = 0;
};

static void ota_note_phase(int phase, int total, int progress)
{
    s_ota_breadcrumb.magic = kOtaBreadcrumbMagic;
    s_ota_breadcrumb.phase = phase;
    s_ota_breadcrumb.total = total;
    s_ota_breadcrumb.progress = progress;
}

static void ota_set_status(int state, const char *text, int progress = -1, uint32_t hold_ms = 0)
{
    g_ota_reboot_pending = false;
    g_ota_state = state;
    g_ota_progress = progress;
    strlcpy(g_ota_status, ota_status_text_or_fallback(text), sizeof(g_ota_status));
    g_ota_status_until_tick = hold_ms > 0 ? xTaskGetTickCount() + pdMS_TO_TICKS(hold_ms) : 0;
    notify_ui_task();
}

static void ota_set_failed_status(const char *text, uint32_t hold_ms = kOtaFailureHoldMs)
{
    ota_set_status(kOtaFailed, text, -1, hold_ms);
}

static void enter_ota_reboot_quiet_window()
{
    g_ota_reboot_pending = true;
    notify_ui_task();
    vTaskDelay(pdMS_TO_TICKS(kOtaPreRestartDisplayQuietMs));
}

static void load_cached_manifest(OtaManifest *manifest)
{
    if (!manifest) {
        return;
    }
    strlcpy(manifest->version, g_ota_version, sizeof(manifest->version));
    strlcpy(manifest->url, g_ota_url, sizeof(manifest->url));
    strlcpy(manifest->sha256, g_ota_sha256, sizeof(manifest->sha256));
    strlcpy(manifest->notes, g_ota_notes, sizeof(manifest->notes));
    manifest->size = g_ota_size;
}

static void store_cached_manifest(const OtaManifest &manifest)
{
    strlcpy(g_ota_version, manifest.version, sizeof(g_ota_version));
    strlcpy(g_ota_url, manifest.url, sizeof(g_ota_url));
    strlcpy(g_ota_sha256, manifest.sha256, sizeof(g_ota_sha256));
    strlcpy(g_ota_notes, manifest.notes, sizeof(g_ota_notes));
    g_ota_size = manifest.size;
}

static void cleanup_ota_http_client(esp_http_client_handle_t *client)
{
    if (!client || !*client) {
        return;
    }
    esp_http_client_cleanup(*client);
    *client = nullptr;
}

static void close_ota_http_client(esp_http_client_handle_t *client)
{
    if (!client || !*client) {
        return;
    }
    esp_http_client_close(*client);
    cleanup_ota_http_client(client);
}

static bool set_ota_event_bit(EventBits_t bit, const char *name)
{
    if (!g_app_events) {
        ESP_LOGW(TAG, OTA_REQUEST_EVENT_GROUP_UNAVAILABLE_FORMAT, ota_request_name_or_fallback(name));
        ota_set_failed_status(kOtaStatusUnavailable);
        return false;
    }
    xEventGroupSetBits(g_app_events, bit);
    return true;
}

static void keep_ota_settings_panel_visible()
{
    TickType_t now = xTaskGetTickCount();
    g_settings_requested = true;
    g_settings_focus_secondary = true;
    g_settings_page_toggle_mode = false;
    g_settings_page_order_mode = false;
    g_settings_primary_selection = kSettingsPrimarySystem;
    g_settings_selection = kSystemSettingsOtaItem;
    g_settings_last_activity_tick = now;
    g_info_page_until_tick = 0;
}

static bool is_http_redirect_status(int status)
{
    return status == kHttpStatusMovedPermanently ||
           status == kHttpStatusFound ||
           status == kHttpStatusSeeOther ||
           status == kHttpStatusTemporaryRedirect ||
           status == kHttpStatusPermanentRedirect;
}

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    if (!evt) {
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_HEADER || !evt->user_data) {
        return ESP_OK;
    }
    OtaHttpContext *ctx = (OtaHttpContext *)evt->user_data;
    if (!ctx->redirect_url || ctx->redirect_url_len == 0 ||
        !evt->header_key || !evt->header_value) {
        return ESP_OK;
    }
    if (strcasecmp(evt->header_key, "Location") == 0) {
        strlcpy(ctx->redirect_url, evt->header_value, ctx->redirect_url_len);
    }
    return ESP_OK;
}

static bool ota_status_hold_active(TickType_t now)
{
    return g_ota_status_until_tick != 0 && now < g_ota_status_until_tick;
}

static bool ota_flow_active_at(TickType_t now)
{
    return g_ota_state == kOtaChecking ||
           (g_ota_state == kOtaAvailable && ota_status_hold_active(now)) ||
           g_ota_state == kOtaUpdating ||
           (g_ota_state == kOtaSucceeded && ota_status_hold_active(now));
}

bool ota_flow_active()
{
    return ota_flow_active_at(xTaskGetTickCount());
}

void ota_reset_status_if_idle()
{
    TickType_t now = xTaskGetTickCount();
    if (!ota_flow_active_at(now) &&
        g_ota_state != kOtaIdle &&
        g_ota_status_until_tick != 0 &&
        now >= g_ota_status_until_tick) {
        g_ota_state = kOtaIdle;
        g_ota_status_until_tick = 0;
    }
    if (g_ota_state == kOtaIdle) {
        strlcpy(g_ota_status, kOtaStatusIdlePrompt, sizeof(g_ota_status));
        g_ota_progress = -1;
        g_ota_speed_kbps = -1;
    }
}

void ota_handle_info_key()
{
    ota_reset_status_if_idle();
    if (g_offline_mode_ui_enabled) {
        keep_ota_settings_panel_visible();
        ota_set_failed_status(kOtaStatusOfflineMode, kOtaOfflineHoldMs);
        return;
    }
    if (g_ota_state == kOtaChecking || g_ota_state == kOtaUpdating) {
        return;
    }
    keep_ota_settings_panel_visible();
    if (g_ota_state == kOtaAvailable) {
        if (!set_ota_event_bit(kOtaInstallBit, "install")) {
            return;
        }
        g_ota_speed_kbps = -1;
        ota_set_status(kOtaUpdating, kOtaStatusInstallingUpdate, 0);
        g_info_page_until_tick = 0;
        return;
    }
    if (!set_ota_event_bit(kOtaCheckBit, "check")) {
        return;
    }
    ota_set_status(kOtaChecking, kOtaStatusCheckingUpdate);
    g_info_page_until_tick = 0;
}

void ota_mark_running_app_valid()
{
    if (s_ota_breadcrumb.magic == kOtaBreadcrumbMagic) {
        ESP_LOGW(TAG,
                 OTA_PREVIOUS_BREADCRUMB_FORMAT,
                 s_ota_breadcrumb.phase,
                 s_ota_breadcrumb.total,
                 s_ota_breadcrumb.progress,
                 (int)esp_reset_reason());
        s_ota_breadcrumb.magic = 0;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s", kOtaAppMarkedValidLog);
        } else {
            ESP_LOGW(TAG, OTA_APP_VALID_MARK_FAILED_FORMAT, esp_err_to_name(err));
        }
    }
}

static int parse_semver_component(const char **cursor)
{
    if (!cursor || !*cursor) {
        return 0;
    }
    int value = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10 + (**cursor - '0');
        ++(*cursor);
    }
    if (**cursor == '.') {
        ++(*cursor);
    }
    return value;
}

static int compare_versions(const char *remote, const char *current)
{
    if (!remote || !current) {
        return 0;
    }
    if (*remote == 'v' || *remote == 'V') ++remote;
    if (*current == 'v' || *current == 'V') ++current;
    for (int i = 0; i < kSemverComponentCount; ++i) {
        int r = parse_semver_component(&remote);
        int c = parse_semver_component(&current);
        if (r != c) {
            return r > c ? 1 : -1;
        }
    }
    return strcmp(remote, current);
}

static bool valid_sha256_string(const char *text)
{
    if (!text) {
        return false;
    }
    if (strlen(text) != kSha256HexLen) {
        return false;
    }
    for (const char *p = text; *p; ++p) {
        if (!((*p >= '0' && *p <= '9') ||
              (*p >= 'a' && *p <= 'f') ||
              (*p >= 'A' && *p <= 'F'))) {
            return false;
        }
    }
    return true;
}

static void sha256_to_hex(const uint8_t *hash, char *out, size_t out_len)
{
    static const char kHex[] = "0123456789abcdef";
    if (!out) {
        return;
    }
    if (out_len <= kSha256HexLen) {
        if (out_len > 0) out[0] = '\0';
        return;
    }
    if (!hash) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < kSha256ByteCount; ++i) {
        out[i * 2] = kHex[hash[i] >> 4];
        out[i * 2 + 1] = kHex[hash[i] & 0x0F];
    }
    out[kSha256HexLen] = '\0';
}

static bool parse_ota_manifest(const char *json, OtaManifest *manifest)
{
    if (!json || !manifest) {
        ESP_LOGW(TAG, "%s", kOtaManifestParseInvalidArgLog);
        return false;
    }
    OtaJsonRoot root(json);
    if (!root) {
        ESP_LOGW(TAG, "%s", kOtaManifestJsonParseFailedLog);
        return false;
    }
    bool have_version = json_copy_string(root.get(),
                                         kOtaManifestJsonVersionField,
                                         manifest->version,
                                         sizeof(manifest->version)) &&
                        manifest->version[0] != '\0';
    bool have_url = json_copy_string(root.get(),
                                     kOtaManifestJsonUrlField,
                                     manifest->url,
                                     sizeof(manifest->url)) &&
                    manifest->url[0] != '\0';
    bool have_sha = json_copy_string(root.get(),
                                     kOtaManifestJsonSha256Field,
                                     manifest->sha256,
                                     sizeof(manifest->sha256));
    cJSON *size = cJSON_GetObjectItem(root.get(), kOtaManifestJsonSizeField);
    if (cJSON_IsNumber(size)) {
        manifest->size = size->valueint;
    }
    (void)json_copy_string(root.get(),
                           kOtaManifestJsonNotesField,
                           manifest->notes,
                           sizeof(manifest->notes));
    if (!have_version || !have_url || !have_sha) {
        ESP_LOGW(TAG, OTA_MANIFEST_MISSING_REQUIRED_FIELDS_FORMAT,
                 have_version,
                 have_url,
                 have_sha);
        return false;
    }
    if (!valid_sha256_string(manifest->sha256)) {
        ESP_LOGW(TAG, OTA_MANIFEST_SHA_INVALID_FORMAT, (unsigned)strlen(manifest->sha256));
        return false;
    }
    return true;
}

static bool ota_manifest_source_name_valid(const OtaManifestSource &source)
{
    return cstr_nonempty(source.name);
}

static bool ota_manifest_source_url_valid(const OtaManifestSource &source)
{
    return cstr_nonempty(source.url) &&
           strstr(source.url, kOtaPlaceholderManifestHost) == nullptr;
}

static const char *ota_manifest_source_name_or_unknown(const char *name)
{
    return cstr_nonempty(name) ? name : kOtaUnknownManifestSource;
}

static bool ota_manifest_source_valid(const OtaManifestSource &source)
{
    return ota_manifest_source_name_valid(source) &&
           ota_manifest_source_url_valid(source);
}

static bool fetch_ota_manifest_from_source(const OtaManifestSource &source, OtaManifest *manifest)
{
    if (!manifest) {
        ota_set_failed_status(kOtaStatusCheckFailed);
        return false;
    }
    if (!ota_manifest_source_valid(source)) {
        ESP_LOGW(TAG, OTA_MANIFEST_SOURCE_SKIPPED_FORMAT, ota_manifest_source_name_or_unknown(source.name));
        return false;
    }
    OtaManifestResponseBuffer response(kOtaManifestResponseBufferSize);
    if (!response) {
        ESP_LOGW(TAG, "%s", kOtaManifestResponseAllocFailedLog);
        ota_set_failed_status(kOtaStatusCheckFailed);
        return false;
    }
    esp_err_t err = http_get_text(source.url, response.data(), response.size());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, OTA_MANIFEST_FETCH_FAILED_FORMAT, ota_manifest_source_name_or_unknown(source.name), esp_err_to_name(err));
        return false;
    }
    if (!parse_ota_manifest(response.data(), manifest)) {
        ESP_LOGW(TAG,
                 OTA_MANIFEST_PARSE_FAILED_FORMAT,
                 ota_manifest_source_name_or_unknown(source.name));
        return false;
    }
    ESP_LOGI(TAG, OTA_MANIFEST_LOADED_FORMAT, ota_manifest_source_name_or_unknown(source.name), manifest->version);
    return true;
}

static void store_ota_manifest_source_name(char *out, size_t out_len, const char *name)
{
    if (!ota_output_buffer_available(out, out_len)) {
        return;
    }
    strlcpy(out, ota_manifest_source_name_or_unknown(name), out_len);
}

static bool fetch_ota_manifest(OtaManifest *manifest, char *source_name = nullptr, size_t source_name_len = 0)
{
    char custom_url[kOtaUrlLen] = {};
    if (custom_assets_read_ota_manifest_url(custom_url, sizeof(custom_url))) {
        OtaManifestSource custom_source = {kOtaManifestSourceCustom, custom_url};
        if (fetch_ota_manifest_from_source(custom_source, manifest)) {
            store_ota_manifest_source_name(source_name, source_name_len, custom_source.name);
            return true;
        }
    }
    for (const auto &source : kOtaBuiltInManifestSources) {
        if (fetch_ota_manifest_from_source(source, manifest)) {
            store_ota_manifest_source_name(source_name, source_name_len, source.name);
            return true;
        }
    }
    ota_set_failed_status(kOtaStatusCheckFailed);
    return false;
}

static bool ota_backup_manifest_matches_current(const OtaManifest &current,
                                                const OtaManifest &candidate)
{
    const bool versions_match = strcmp(candidate.version, current.version) == 0;
    const bool checksums_match = strcasecmp(candidate.sha256, current.sha256) == 0;
    const bool sizes_match = current.size <= 0 || candidate.size <= 0 || current.size == candidate.size;
    return versions_match && checksums_match && sizes_match;
}

static bool fetch_backup_manifest_for_install(const OtaManifest &current, OtaManifest *backup)
{
    if (!backup || current.version[0] == '\0' || !valid_sha256_string(current.sha256)) {
        return false;
    }
    OtaManifest candidate;
    const OtaManifestSource &backup_source =
        kOtaBuiltInManifestSources[kOtaBackupManifestSourceIndex];
    if (!fetch_ota_manifest_from_source(backup_source, &candidate)) {
        return false;
    }
    if (!ota_backup_manifest_matches_current(current, candidate)) {
        ESP_LOGW(TAG,
                 OTA_BACKUP_MANIFEST_MISMATCH_FORMAT,
                 current.version,
                 candidate.version);
        return false;
    }
    *backup = candidate;
    return true;
}

static bool download_and_apply_ota(const OtaManifest &manifest)
{
    if (manifest.url[0] == '\0' || !valid_sha256_string(manifest.sha256)) {
        ESP_LOGW(TAG, "%s", kOtaManifestInvalidForInstallLog);
        ota_set_failed_status(kOtaStatusDownloadFailed);
        return false;
    }
    ota_note_phase(1, 0, 0);
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ota_set_failed_status(kOtaStatusNoOtaSlot);
        return false;
    }

    wifi_ap_record_t ap_info = {};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    ESP_LOGI(TAG,
             OTA_DOWNLOAD_START_FORMAT,
             (int)esp_reset_reason(),
             g_battery_percent,
             g_battery_voltage,
             rssi,
             manifest.size,
             manifest.url);
    log_ota_heap("start", 0, 0);

    char current_url[kOtaRedirectUrlLen] = {};
    strlcpy(current_url, manifest.url, sizeof(current_url));
    esp_http_client_handle_t client = nullptr;
    int content_len = 0;
    esp_err_t err = ESP_FAIL;
    for (int redirect = 0; redirect <= kOtaMaxRedirects; ++redirect) {
        char redirect_url[kOtaRedirectUrlLen] = {};
        OtaHttpContext http_ctx = {redirect_url, sizeof(redirect_url)};
        esp_http_client_config_t config = {};
        config.url = current_url;
        config.timeout_ms = kOtaHttpTimeoutMs;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.disable_auto_redirect = true;
        config.max_redirection_count = kOtaMaxRedirects;
        config.keep_alive_enable = true;
        config.buffer_size = kOtaDownloadBufferSize;
        config.buffer_size_tx = kOtaHttpTxBufferSize;
        config.event_handler = ota_http_event_handler;
        config.user_data = &http_ctx;

        client = esp_http_client_init(&config);
        if (!client) {
            ota_set_failed_status(kOtaStatusDownloadFailed);
            return false;
        }
        esp_http_client_set_header(client, "Accept", "application/octet-stream,*/*");
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, OTA_HTTP_OPEN_FAILED_FORMAT, esp_err_to_name(err));
            cleanup_ota_http_client(&client);
            ota_set_failed_status(kOtaStatusDownloadFailed);
            return false;
        }
        content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (is_http_redirect_status(status)) {
            ESP_LOGI(TAG, OTA_REDIRECT_STATUS_FORMAT, status, redirect_url[0] ? redirect_url : "--");
            close_ota_http_client(&client);
            if (redirect_url[0] == '\0' || strlen(redirect_url) >= sizeof(current_url)) {
                ota_set_failed_status(kOtaStatusDownloadFailed);
                return false;
            }
            strlcpy(current_url, redirect_url, sizeof(current_url));
            continue;
        }
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, OTA_HTTP_STATUS_FAILED_FORMAT, status, content_len);
            close_ota_http_client(&client);
            ota_set_failed_status(kOtaStatusDownloadFailed);
            return false;
        }
        break;
    }
    if (!client) {
        ESP_LOGW(TAG, "%s", kOtaRedirectLimitReachedLog);
        ota_set_failed_status(kOtaStatusDownloadFailed);
        return false;
    }

    esp_ota_handle_t ota_handle = 0;
    ota_note_phase(2, 0, 0);
    err = esp_ota_begin(update_partition, manifest.size > 0 ? manifest.size : OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, OTA_BEGIN_FAILED_FORMAT, esp_err_to_name(err));
        close_ota_http_client(&client);
        ota_set_failed_status(kOtaStatusUpdateFailed);
        return false;
    }

    OtaDownloadBuffer buffer(kOtaDownloadBufferSize);
    if (!buffer) {
        esp_ota_abort(ota_handle);
        close_ota_http_client(&client);
        ota_set_failed_status(kOtaStatusNoMemory);
        return false;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    int total = 0;
    int last_progress = -1;
    int last_heap_progress = -25;
    int64_t started_us = esp_timer_get_time();
    int64_t last_progress_us = started_us;
    int64_t last_status_us = 0;
    int last_status_total = 0;
    bool ok = true;
    OtaTaskWatchdogGuard wdt;
    for (;;) {
        wdt.reset();
        int64_t now_us = esp_timer_get_time();
        if (now_us - started_us > (int64_t)kOtaMaxDownloadMs * kOtaUsPerMs) {
            ESP_LOGW(TAG, OTA_DOWNLOAD_TIMEOUT_FORMAT, total);
            ok = false;
            break;
        }
        int read = esp_http_client_read(client, (char *)buffer.data(), buffer.size());
        wdt.reset();
        if (read < 0) {
            if (esp_timer_get_time() - last_progress_us > (int64_t)kOtaNoProgressTimeoutMs * kOtaUsPerMs) {
                ESP_LOGW(TAG, OTA_READ_FAILED_NO_PROGRESS_FORMAT, total);
                ok = false;
                break;
            }
            vTaskDelay(kOtaReadRetryDelay);
            continue;
        }
        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            if (esp_timer_get_time() - last_progress_us > (int64_t)kOtaNoProgressTimeoutMs * kOtaUsPerMs) {
                ESP_LOGW(TAG, OTA_STALLED_FORMAT, total);
                ok = false;
                break;
            }
            vTaskDelay(kOtaReadRetryDelay);
            continue;
        }
        last_progress_us = esp_timer_get_time();
        mbedtls_sha256_update(&sha_ctx, buffer.data(), read);
        err = esp_ota_write(ota_handle, buffer.data(), read);
        wdt.reset();
        if (err != ESP_OK) {
            ok = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kOtaChunkDelayMs));
        total += read;
        int expected = content_len > 0 ? content_len : manifest.size;
        if (expected > 0) {
            int progress = (total * 100) / expected;
            if (progress > 100) progress = 100;
            if (progress != last_progress) {
                ota_note_phase(3, total, progress);
            }
            if (progress >= last_heap_progress + 25 || progress >= 100) {
                log_ota_heap("download", total, progress);
                last_heap_progress = progress;
            }
            int64_t now_us = esp_timer_get_time();
            bool progress_step = progress != last_progress;
            bool status_due = last_status_us == 0 ||
                              now_us - last_status_us >= (int64_t)kOtaStatusMinIntervalMs * kOtaUsPerMs ||
                              progress >= 100;
            if (status_due) {
                int speed_window_bytes = total - last_status_total;
                int64_t speed_window_us = last_status_us == 0 ? now_us - started_us : now_us - last_status_us;
                int speed_kbps = ota_speed_kbps_for_window(speed_window_bytes, speed_window_us);
                char status_text[kOtaDownloadStatusTextLen] = {};
                format_ota_status_text(status_text,
                                       sizeof(status_text),
                                       kOtaStatusInstallingProgressFormat,
                                       progress,
                                       speed_kbps);
                g_ota_speed_kbps = speed_kbps;
                ota_set_status(kOtaUpdating, status_text, progress);
                last_status_us = now_us;
                last_status_total = total;
                if (progress_step) {
                    last_progress = progress;
                }
            }
        }
    }

    uint8_t hash[kSha256ByteCount];
    wdt.reset();
    mbedtls_sha256_finish(&sha_ctx, hash);
    mbedtls_sha256_free(&sha_ctx);
    bool complete = esp_http_client_is_complete_data_received(client);
    close_ota_http_client(&client);

    if (!ok || !complete) {
        esp_ota_abort(ota_handle);
        ota_set_failed_status(kOtaStatusDownloadFailed);
        return false;
    }

    char actual_sha[kOtaSha256Len] = {};
    sha256_to_hex(hash, actual_sha, sizeof(actual_sha));
    ota_note_phase(4, total, 100);
    if (strcasecmp(actual_sha, manifest.sha256) != 0) {
        ESP_LOGW(TAG, OTA_SHA_MISMATCH_FORMAT, manifest.sha256, actual_sha);
        esp_ota_abort(ota_handle);
        ota_set_failed_status(kOtaStatusVerifyFailed);
        return false;
    }

    wdt.reset();
    ota_note_phase(5, total, 100);
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, OTA_END_FAILED_FORMAT, esp_err_to_name(err));
        ota_set_failed_status(kOtaStatusUpdateFailed);
        return false;
    }
    esp_app_desc_t app_desc = {};
    err = esp_ota_get_partition_description(update_partition, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, OTA_APP_DESCRIPTION_FAILED_FORMAT, esp_err_to_name(err));
        ota_set_failed_status(kOtaStatusVerifyFailed);
        return false;
    }
    ESP_LOGI(TAG, OTA_IMAGE_READY_FORMAT, app_desc.version, app_desc.project_name);
    wdt.reset();
    ota_note_phase(6, total, 100);
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, OTA_BOOT_PARTITION_FAILED_FORMAT, esp_err_to_name(err));
        ota_set_failed_status(kOtaStatusUpdateFailed);
        return false;
    }

    s_ota_breadcrumb.magic = 0;
    return true;
}

static bool prepare_ota_wifi()
{
    if (!g_have_wifi_creds) {
        ota_set_failed_status(kOtaStatusNoWifi);
        return false;
    }
    if (g_low_battery_mode || (g_battery_percent >= 0 && g_battery_percent < kLowBatteryEnterPercent)) {
        ota_set_failed_status(kOtaStatusLowBattery);
        return false;
    }
    acquire_network_awake_lock();
    if (!start_wifi_radio(false)) {
        release_network_awake_lock();
        ota_set_failed_status(kOtaStatusWifiFailed);
        return false;
    }
    if (!wait_for_wifi_connected(kOtaWifiConnectTimeoutMs)) {
        stop_wifi_radio();
        release_network_awake_lock();
        ota_set_failed_status(kOtaStatusWifiFailed);
        return false;
    }
    return true;
}

static void finish_ota_wifi(bool keep_awake_lock = false)
{
    stop_wifi_radio(true);
    if (!keep_awake_lock) {
        release_network_awake_lock();
    }
}

void ota_task(void *)
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kOtaTaskEventGroupUnavailableLog);
        vTaskDelete(nullptr);
        return;
    }
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(g_app_events,
                                               kOtaCheckBit | kOtaInstallBit,
                                               pdTRUE,
                                               pdFALSE,
                                               portMAX_DELAY);
        bool install = (bits & kOtaInstallBit) != 0;
        bool check = (bits & kOtaCheckBit) != 0;
        if (!install && !check) {
            continue;
        }

        if (!prepare_ota_wifi()) {
            g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kOtaFailureHoldMs);
            continue;
        }

        OtaManifest manifest;
        if (install) {
            load_cached_manifest(&manifest);
        } else {
            ota_set_status(kOtaChecking, kOtaStatusCheckingUpdate);
            char manifest_source[kOtaManifestSourceNameLen] = {};
            if (!fetch_ota_manifest(&manifest, manifest_source, sizeof(manifest_source))) {
                finish_ota_wifi();
                g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kOtaFailureHoldMs);
                continue;
            }
            ESP_LOGI(TAG,
                     OTA_UPDATE_CHECK_FORMAT,
                     ota_manifest_source_name_or_unknown(manifest_source),
                     manifest.version,
                     APP_VERSION);
            if (compare_versions(manifest.version, APP_VERSION) <= 0) {
                ota_set_status(kOtaNoUpdate, kOtaStatusAlreadyLatest, -1, kOtaFailureHoldMs);
                finish_ota_wifi();
                g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kOtaFailureHoldMs);
                continue;
            }
            store_cached_manifest(manifest);
            char status_text[kOtaStatusLen] = {};
            format_ota_status_text(status_text, sizeof(status_text), kOtaStatusNewVersionFormat, manifest.version);
            ota_set_status(kOtaAvailable, status_text, -1, kOtaAvailableConfirmTimeoutMs);
            finish_ota_wifi();
            continue;
        }

        bool ok = false;
        {
            OtaDisplayQuietGuard display_quiet;
            ok = download_and_apply_ota(manifest);
            if (!ok) {
                OtaManifest backup_manifest;
                if (fetch_backup_manifest_for_install(manifest, &backup_manifest) &&
                    strcmp(backup_manifest.url, manifest.url) != 0) {
                    ESP_LOGW(TAG, "%s", kOtaPrimaryDownloadRetryBackupLog);
                    ota_set_status(kOtaUpdating, kOtaStatusInstallingBackup, 0);
                    ok = download_and_apply_ota(backup_manifest);
                }
            }
            finish_ota_wifi(ok);
            if (ok) {
                keep_ota_settings_panel_visible();
                ota_set_status(kOtaSucceeded, kOtaStatusUpdateDoneRebooting, 100, kOtaSuccessHoldMs);
                vTaskDelay(pdMS_TO_TICKS(kOtaRebootNoticeDelayMs));
                enter_ota_reboot_quiet_window();
                esp_restart();
                release_network_awake_lock();
            }
        }
        if (!ok) {
            g_info_page_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kOtaFailureHoldMs);
        }
    }
}
