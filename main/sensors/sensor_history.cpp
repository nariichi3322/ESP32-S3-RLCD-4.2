// 维护本地温湿度趋势和 24 小时历史样本的内存与 NVS 数据。
#include "sensor_services.h"

#include "app_constexpr.h"
#include "app_text_format.h"
#include "sensor_history_format.h"
#include "sensor_trend.h"

#include "ui_views.h"

#define HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT "hourly sensor slot key index invalid: %d"
#define HOURLY_SLOT_KEY_TRUNCATED_LOG_FORMAT "hourly sensor slot key truncated index=%d"
#define SENSOR_HISTORY_NVS_OPEN_FAILED_LOG_FORMAT "open sensor history nvs failed: %s"
#define HOURLY_SLOT_READ_FAILED_LOG_FORMAT "read hourly sensor slot %s failed: %s"
#define HOURLY_SLOT_INVALID_LOG_FORMAT "hourly sensor slot %s blob invalid"
#define HOURLY_META_READ_FAILED_LOG_FORMAT "read hourly sensor meta failed: %s"
constexpr const char *kHourlyMetaInvalidLog = "hourly sensor meta blob invalid";
#define LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT "read legacy hourly sensor history failed: %s"
constexpr const char *kLegacyHourlyHistoryInvalidLog = "legacy hourly sensor history blob invalid";
#define HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT "hourly sensor slot index invalid: %d"
#define SENSOR_NVS_OPEN_FAILED_LOG_FORMAT "open sensor nvs failed: %s"
#define HOURLY_SLOT_SAVE_FAILED_LOG_FORMAT "save hourly sensor slot failed: %s"
#define HOURLY_SNAPSHOT_INVALID_ARG_LOG "hourly sensor snapshot invalid arg"

namespace {
using sensor_history_format::HourlySensorHistoryMeta;
using sensor_history_format::LegacyHourlySensorHistoryBlob;

constexpr const char *kSensorNvsNamespace = "sensor";
constexpr const char *kHourlyHistoryMetaKey = "hourmeta";
constexpr const char *kLegacyHourlyHistoryKey = "hourly24";
constexpr const char *kHourlySlotKeyFormat = "h%02d";
constexpr size_t kHourlySlotKeyBufferSize = 8;
constexpr int kMsPerSecond = 1000;
constexpr int kUsPerMs = 1000;
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kSensorTrendWindowHours = 4;
constexpr int64_t kSensorTrendWindowMs = (int64_t)kSensorTrendWindowHours * kMinutesPerHour * kSecondsPerMinute * kMsPerSecond;
constexpr const char *kSensorHistoryTexts[] = {
    kSensorNvsNamespace,
    kHourlyHistoryMetaKey,
    kLegacyHourlyHistoryKey,
    kHourlySlotKeyFormat,
    HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT,
    HOURLY_SLOT_KEY_TRUNCATED_LOG_FORMAT,
    SENSOR_HISTORY_NVS_OPEN_FAILED_LOG_FORMAT,
    HOURLY_SLOT_READ_FAILED_LOG_FORMAT,
    HOURLY_SLOT_INVALID_LOG_FORMAT,
    HOURLY_META_READ_FAILED_LOG_FORMAT,
    kHourlyMetaInvalidLog,
    LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT,
    kLegacyHourlyHistoryInvalidLog,
    HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT,
    SENSOR_NVS_OPEN_FAILED_LOG_FORMAT,
    HOURLY_SLOT_SAVE_FAILED_LOG_FORMAT,
    HOURLY_SNAPSHOT_INVALID_ARG_LOG,
};
portMUX_TYPE s_hourly_history_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_local_sensor_mux = portMUX_INITIALIZER_UNLOCKED;

static_assert(kHourlyHistoryCount > 0, "hourly history must keep at least one slot");
static_assert(kLegacyHourlyHistoryCount > 0, "legacy hourly history must keep at least one sample");
static_assert(kHourlyHistoryCount <= 99, "hourly slot key format h%02d supports two-digit indexes");
static_assert(kHourlyHistoryCount >= kLegacyHourlyHistoryCount,
              "new hourly history must cover legacy history samples");
static_assert(sensor_history_format::kHourlyHistoryMetaVersion >
                  sensor_history_format::kLegacyHourlyHistoryVersion,
              "hourly sensor history meta version must be newer than legacy blob version");
static_assert(kHourlySlotKeyBufferSize >= sizeof("h00"), "hourly slot key buffer must fit hNN plus terminator");
static_assert(array_count(kSensorHistoryTexts) > 0,
              "sensor history text registry must not be empty");
static_assert(cstr_array_nonempty(kSensorHistoryTexts), "sensor history NVS strings and logs must be non-empty");
static_assert(kSensorSampleDayMinutes > 0, "day sensor sample interval must be positive");
static_assert(kSensorSampleNightMinutes > 0, "night sensor sample interval must be positive");
static_assert(kSensorSampleNightMinutes >= kSensorSampleDayMinutes,
              "night sensor sample interval must not be faster than day interval");
static_assert(kSensorTrendWindowHours > 0, "sensor trend window must be positive");
static_assert(kSensorTrendWindowMs > 0, "sensor trend window in ms must be positive");
static_assert(kSensorHistoryMinutes >= (kSensorTrendWindowHours * kMinutesPerHour) / kSensorSampleDayMinutes,
              "sensor trend history must cover the full day-sampling trend window");
bool should_log_nvs_read_error(esp_err_t err)
{
    return err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND;
}

void store_loaded_hourly_sample(int index, const HourlySensorSample &sample, int64_t *newest_slot)
{
    g_hourly_history.samples[index] = sample;
    if (newest_slot && sample.valid && sample.timestamp > *newest_slot) {
        *newest_slot = sample.timestamp;
    }
}

void store_legacy_hourly_history_samples(const LegacyHourlySensorHistoryBlob &legacy)
{
    for (int i = 0; i < kLegacyHourlyHistoryCount; ++i) {
        store_loaded_hourly_sample(i, legacy.samples[i], &g_last_hourly_saved_at);
    }
}

int64_t sensor_trend_now_ms()
{
    return esp_timer_get_time() / kUsPerMs;
}

void append_sensor_history_sample(float temp, float humi)
{
    g_sensor_history[g_sensor_history_next].sampled_at_ms = sensor_trend_now_ms();
    g_sensor_history[g_sensor_history_next].temperature = temp;
    g_sensor_history[g_sensor_history_next].humidity = humi;
    g_sensor_history_next = (g_sensor_history_next + 1) % kSensorHistoryMinutes;
    if (g_sensor_history_count < kSensorHistoryMinutes) {
        ++g_sensor_history_count;
    }
}

SensorTrendAverage calculate_sensor_history_average()
{
    return calculate_sensor_trend_average(g_sensor_history,
                                          g_sensor_history_count,
                                          kSensorHistoryMinutes,
                                          sensor_trend_now_ms(),
                                          kSensorTrendWindowMs);
}

void calculate_sensor_trend_from_average(const SensorTrendAverage &average,
                                          int *temperature_trend,
                                          int *humidity_trend)
{
    if (!temperature_trend || !humidity_trend) {
        return;
    }
    if (average.count <= 0) {
        *temperature_trend = 0;
        *humidity_trend = 0;
        g_sensor_average_valid = false;
        return;
    }
    calculate_sensor_trend_directions(average,
                                      g_sensor_average_valid,
                                      g_last_temp_average,
                                      g_last_humi_average,
                                      kTrendEpsilon,
                                      temperature_trend,
                                      humidity_trend);
    g_last_temp_average = average.temperature;
    g_last_humi_average = average.humidity;
    g_sensor_average_valid = true;
}
} // namespace

static bool hourly_slot_key(int index, char *out, size_t out_len)
{
    if (!app_text::output_buffer_available(out, out_len)) {
        return false;
    }
    if (!sensor_history_format::hourly_index_valid(index)) {
        out[0] = '\0';
        ESP_LOGW(TAG, HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT, index);
        return false;
    }
    int written = snprintf(out, out_len, kHourlySlotKeyFormat, index);
    if (app_text::format_failed(written, out_len)) {
        out[0] = '\0';
        ESP_LOGW(TAG, HOURLY_SLOT_KEY_TRUNCATED_LOG_FORMAT, index);
        return false;
    }
    return true;
}

static bool load_hourly_sensor_slot(nvs_handle_t nvs, int index, int64_t *newest_slot)
{
    HourlySensorSample sample = {};
    size_t sample_len = sizeof(sample);
    char key[kHourlySlotKeyBufferSize] = {};
    if (!hourly_slot_key(index, key, sizeof(key))) {
        return false;
    }
    esp_err_t err = nvs_get_blob(nvs, key, &sample, &sample_len);
    if (err == ESP_OK && sample_len == sizeof(sample)) {
        store_loaded_hourly_sample(index, sample, newest_slot);
        return true;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, HOURLY_SLOT_INVALID_LOG_FORMAT, key);
        return false;
    }
    if (should_log_nvs_read_error(err)) {
        ESP_LOGW(TAG, HOURLY_SLOT_READ_FAILED_LOG_FORMAT, key, esp_err_to_name(err));
    }
    return false;
}

static inline bool load_current_hourly_sensor_slots(nvs_handle_t nvs,
                                                    const HourlySensorHistoryMeta &meta)
{
    int loaded = 0;
    int64_t newest_slot = 0;
    for (int i = 0; i < kHourlyHistoryCount; ++i) {
        if (load_hourly_sensor_slot(nvs, i, &newest_slot)) {
            ++loaded;
        }
    }
    if (loaded <= 0) {
        return false;
    }
    g_last_hourly_saved_at = newest_slot;
    if (meta.last_saved_at > g_last_hourly_saved_at) {
        g_last_hourly_saved_at = meta.last_saved_at;
    }
    return true;
}

static inline bool read_legacy_hourly_sensor_history(nvs_handle_t nvs,
                                                     LegacyHourlySensorHistoryBlob *legacy)
{
    if (!legacy) {
        return false;
    }
    size_t legacy_len = sizeof(*legacy);
    esp_err_t err = nvs_get_blob(nvs, kLegacyHourlyHistoryKey, legacy, &legacy_len);
    if (err != ESP_OK) {
        if (should_log_nvs_read_error(err)) {
            ESP_LOGW(TAG, LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
        return false;
    }
    if (!sensor_history_format::legacy_history_valid(*legacy, legacy_len)) {
        ESP_LOGW(TAG, "%s", kLegacyHourlyHistoryInvalidLog);
        return false;
    }
    return true;
}

static esp_err_t save_hourly_sensor_meta_and_slot(nvs_handle_t nvs,
                                                  int index,
                                                  int64_t last_saved_at,
                                                  const HourlySensorSample &sample)
{
    HourlySensorHistoryMeta meta = {};
    meta.last_saved_at = last_saved_at;
    esp_err_t err = nvs_set_blob(nvs, kHourlyHistoryMetaKey, &meta, sizeof(meta));
    if (err != ESP_OK) {
        return err;
    }

    char key[kHourlySlotKeyBufferSize] = {};
    if (!hourly_slot_key(index, key, sizeof(key))) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_blob(nvs, key, &sample, sizeof(sample));
}

void reset_hourly_sensor_history()
{
    portENTER_CRITICAL(&s_hourly_history_mux);
    memset(&g_hourly_history, 0, sizeof(g_hourly_history));
    g_hourly_history.magic = kHourlyHistoryMagic;
    g_hourly_history.version = sensor_history_format::kLegacyHourlyHistoryVersion;
    g_hourly_history.count = kHourlyHistoryCount;
    g_last_hourly_saved_at = 0;
    ++g_hourly_history_version;
    portEXIT_CRITICAL(&s_hourly_history_mux);
}

void load_hourly_sensor_history()
{
    reset_hourly_sensor_history();
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kSensorNvsNamespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        if (should_log_nvs_read_error(err)) {
            ESP_LOGW(TAG, SENSOR_HISTORY_NVS_OPEN_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
        return;
    }
    HourlySensorHistoryMeta meta = {};
    size_t meta_len = sizeof(meta);
    err = nvs_get_blob(nvs, kHourlyHistoryMetaKey, &meta, &meta_len);
    bool meta_valid = err == ESP_OK && sensor_history_format::hourly_meta_valid(meta, meta_len);
    if (meta_valid && load_current_hourly_sensor_slots(nvs, meta)) {
        nvs_close(nvs);
        ++g_hourly_history_version;
        return;
    }
    if (err == ESP_OK && !meta_valid) {
        ESP_LOGW(TAG, "%s", kHourlyMetaInvalidLog);
    } else if (should_log_nvs_read_error(err)) {
        ESP_LOGW(TAG, HOURLY_META_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
    }

    LegacyHourlySensorHistoryBlob legacy = {};
    bool legacy_loaded = read_legacy_hourly_sensor_history(nvs, &legacy);
    nvs_close(nvs);
    if (!legacy_loaded) {
        return;
    }
    store_legacy_hourly_history_samples(legacy);
    ++g_hourly_history_version;
}

static bool save_hourly_sensor_slot(int index,
                                    int64_t last_saved_at,
                                    const HourlySensorSample &sample)
{
    if (!sensor_history_format::hourly_index_valid(index)) {
        ESP_LOGW(TAG, HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT, index);
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kSensorNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, SENSOR_NVS_OPEN_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return false;
    }
    err = save_hourly_sensor_meta_and_slot(nvs, index, last_saved_at, sample);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, HOURLY_SLOT_SAVE_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return false;
    }
    return true;
}

void record_hourly_sensor_sample(float temp, float humi)
{
    struct tm local = {};
    if (!is_system_time_plausible(&local)) {
        return;
    }
    time_t now = mktime(&local);
    time_t hour_start = hour_start_from_time(now);
    portENTER_CRITICAL(&s_hourly_history_mux);
    bool already_saved = hour_start == g_last_hourly_saved_at;
    portEXIT_CRITICAL(&s_hourly_history_mux);
    if (hour_start <= 0 || already_saved) {
        return;
    }
    int index = sensor_history_format::hourly_slot_index_for_time(hour_start);
    HourlySensorSample sample = {};
    sample.timestamp = hour_start;
    sample.temperature = temp;
    sample.humidity = humi;
    sample.valid = 1;
    if (!save_hourly_sensor_slot(index, hour_start, sample)) {
        return;
    }
    portENTER_CRITICAL(&s_hourly_history_mux);
    g_hourly_history.samples[index] = sample;
    g_last_hourly_saved_at = hour_start;
    ++g_hourly_history_version;
    portEXIT_CRITICAL(&s_hourly_history_mux);
    notify_ui_task();
}

bool get_hourly_sensor_history_snapshot(HourlySensorHistoryBlob *history, uint32_t *version)
{
    if (!history && !version) {
        ESP_LOGW(TAG, "%s", HOURLY_SNAPSHOT_INVALID_ARG_LOG);
        return false;
    }
    portENTER_CRITICAL(&s_hourly_history_mux);
    if (history) {
        *history = g_hourly_history;
    }
    if (version) {
        *version = g_hourly_history_version;
    }
    portEXIT_CRITICAL(&s_hourly_history_mux);
    return true;
}

static void calculate_updated_sensor_trends(float temp,
                                            float humi,
                                            int *temperature_trend,
                                            int *humidity_trend)
{
    append_sensor_history_sample(temp, humi);
    calculate_sensor_trend_from_average(calculate_sensor_history_average(),
                                        temperature_trend,
                                        humidity_trend);
}

void update_sensor_history(float temp, float humi)
{
    int temperature_trend = 0;
    int humidity_trend = 0;
    calculate_updated_sensor_trends(temp, humi, &temperature_trend, &humidity_trend);
    portENTER_CRITICAL(&s_local_sensor_mux);
    g_temp_trend = temperature_trend;
    g_humi_trend = humidity_trend;
    portEXIT_CRITICAL(&s_local_sensor_mux);
}

void sample_sensor()
{
    float temp = 0.0f;
    float humi = 0.0f;
    bool sensor_ok = g_shtc3 && g_shtc3->Shtc3_ReadTempHumi(&temp, &humi) == 0;
    if (sensor_ok) {
        int temperature_trend = 0;
        int humidity_trend = 0;
        calculate_updated_sensor_trends(temp, humi, &temperature_trend, &humidity_trend);
        portENTER_CRITICAL(&s_local_sensor_mux);
        g_sensor_ok = true;
        g_temperature = temp;
        g_humidity = humi;
        g_temp_trend = temperature_trend;
        g_humi_trend = humidity_trend;
        portEXIT_CRITICAL(&s_local_sensor_mux);
        record_hourly_sensor_sample(temp, humi);
    } else {
        portENTER_CRITICAL(&s_local_sensor_mux);
        g_sensor_ok = false;
        portEXIT_CRITICAL(&s_local_sensor_mux);
    }
}

bool get_local_sensor_snapshot(float *temperature,
                               float *humidity,
                               int *temperature_trend,
                               int *humidity_trend)
{
    portENTER_CRITICAL(&s_local_sensor_mux);
    bool sensor_ok = g_sensor_ok;
    if (temperature) {
        *temperature = g_temperature;
    }
    if (humidity) {
        *humidity = g_humidity;
    }
    if (temperature_trend) {
        *temperature_trend = g_temp_trend;
    }
    if (humidity_trend) {
        *humidity_trend = g_humi_trend;
    }
    portEXIT_CRITICAL(&s_local_sensor_mux);
    return sensor_ok;
}
