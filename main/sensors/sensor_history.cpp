// 维护本地温湿度趋势和 24 小时历史样本的内存与 NVS 数据。
#include "sensor_services.h"

#include "ui_views.h"

#include <cstddef>

#define SENSOR_INTERVAL_INVALID_LOG_FORMAT "sensor interval invalid: %d"
#define HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT "hourly sensor slot key index invalid: %d"
#define HOURLY_SLOT_KEY_TRUNCATED_LOG_FORMAT "hourly sensor slot key truncated index=%d"
#define SENSOR_HISTORY_NVS_OPEN_FAILED_LOG_FORMAT "open sensor history nvs failed: %s"
#define HOURLY_SLOT_READ_FAILED_LOG_FORMAT "read hourly sensor slot %s failed: %s"
#define HOURLY_META_READ_FAILED_LOG_FORMAT "read hourly sensor meta failed: %s"
#define LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT "read legacy hourly sensor history failed: %s"
#define HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT "hourly sensor slot index invalid: %d"
#define SENSOR_NVS_OPEN_FAILED_LOG_FORMAT "open sensor nvs failed: %s"
#define HOURLY_SLOT_SAVE_FAILED_LOG_FORMAT "save hourly sensor slot failed: %s"

static constexpr uint16_t kHourlyHistoryMetaVersion = 2;
static constexpr uint16_t kLegacyHourlyHistoryVersion = 1;

struct HourlySensorHistoryMeta {
    uint32_t magic = kHourlyHistoryMagic;
    uint16_t version = kHourlyHistoryMetaVersion;
    uint16_t count = kHourlyHistoryCount;
    int64_t last_saved_at = 0;
};

struct LegacyHourlySensorHistoryBlob {
    uint32_t magic = kHourlyHistoryMagic;
    uint16_t version = kLegacyHourlyHistoryVersion;
    uint16_t count = kLegacyHourlyHistoryCount;
    HourlySensorSample samples[kLegacyHourlyHistoryCount] = {};
};

struct SensorHistoryAverage {
    float temperature = 0.0f;
    float humidity = 0.0f;
    int count = 0;
};

bool is_system_time_plausible(struct tm *local_out);
int periodic_sample_minutes(const struct tm &local, int day_minutes, int night_minutes);

namespace {
constexpr const char *kSensorNvsNamespace = "sensor";
constexpr const char *kHourlyHistoryMetaKey = "hourmeta";
constexpr const char *kLegacyHourlyHistoryKey = "hourly24";
constexpr const char *kHourlySlotKeyFormat = "h%02d";
constexpr size_t kHourlySlotKeyBufferSize = 8;
constexpr int kMsPerSecond = 1000;
constexpr int kUsPerMs = 1000;
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
constexpr int kWeatherSyncFallbackSeconds = kSecondsPerHour;
constexpr int kWeatherSyncSearchHours = 30;
constexpr int kWeatherSyncSearchStepHours = 1;
constexpr int kUnknownTimeSensorSampleMs = kSecondsPerMinute * kMsPerSecond;
constexpr int kSensorSampleDayMinutes = 1;
constexpr int kSensorSampleNightMinutes = 2;
constexpr int kSensorTrendWindowHours = 4;
constexpr int64_t kSensorTrendWindowMs = (int64_t)kSensorTrendWindowHours * kMinutesPerHour * kSecondsPerMinute * kMsPerSecond;
constexpr int kNightSlowWindowStartHour = 22;
constexpr int kNightSlowWindowEndHour = 6;
constexpr int kTmYearOffset = 1900;
constexpr const char *kSensorHistoryTexts[] = {
    kSensorNvsNamespace,
    kHourlyHistoryMetaKey,
    kLegacyHourlyHistoryKey,
    kHourlySlotKeyFormat,
    SENSOR_INTERVAL_INVALID_LOG_FORMAT,
    HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT,
    HOURLY_SLOT_KEY_TRUNCATED_LOG_FORMAT,
    SENSOR_HISTORY_NVS_OPEN_FAILED_LOG_FORMAT,
    HOURLY_SLOT_READ_FAILED_LOG_FORMAT,
    HOURLY_META_READ_FAILED_LOG_FORMAT,
    LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT,
    HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT,
    SENSOR_NVS_OPEN_FAILED_LOG_FORMAT,
    HOURLY_SLOT_SAVE_FAILED_LOG_FORMAT,
};

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <std::size_t N>
constexpr std::size_t array_count(const char *const (&)[N])
{
    return N;
}

template <std::size_t N>
constexpr bool cstr_array_nonempty(const char *const (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

static_assert(kHourlyHistoryCount > 0, "hourly history must keep at least one slot");
static_assert(kLegacyHourlyHistoryCount > 0, "legacy hourly history must keep at least one sample");
static_assert(kHourlyHistoryCount <= 99, "hourly slot key format h%02d supports two-digit indexes");
static_assert(kHourlyHistoryCount >= kLegacyHourlyHistoryCount,
              "new hourly history must cover legacy history samples");
static_assert(kHourlyHistoryMetaVersion > kLegacyHourlyHistoryVersion,
              "hourly sensor history meta version must be newer than legacy blob version");
static_assert(kHourlyHistoryMetaVersion != kLegacyHourlyHistoryVersion,
              "new hourly metadata version must differ from legacy blob version");
static_assert(kHourlySlotKeyBufferSize >= sizeof("h00"), "hourly slot key buffer must fit hNN plus terminator");
static_assert(array_count(kSensorHistoryTexts) > 0,
              "sensor history text registry must not be empty");
static_assert(cstr_array_nonempty(kSensorHistoryTexts), "sensor history NVS strings and logs must be non-empty");
static_assert(kWeatherSyncFallbackSeconds > 0, "weather sync fallback interval must be positive");
static_assert(kWeatherSyncSearchHours > 0, "weather sync search hours must be positive");
static_assert(kWeatherSyncSearchStepHours > 0, "weather sync search step must be positive");
static_assert(kUnknownTimeSensorSampleMs > 0, "unknown-time sensor sample interval must be positive");
static_assert(kSensorSampleDayMinutes > 0, "day sensor sample interval must be positive");
static_assert(kSensorSampleNightMinutes > 0, "night sensor sample interval must be positive");
static_assert(kSensorSampleNightMinutes >= kSensorSampleDayMinutes,
              "night sensor sample interval must not be faster than day interval");
static_assert(kSensorTrendWindowHours > 0, "sensor trend window must be positive");
static_assert(kSensorTrendWindowMs > 0, "sensor trend window in ms must be positive");
static_assert(kSensorHistoryMinutes >= (kSensorTrendWindowHours * kMinutesPerHour) / kSensorSampleDayMinutes,
              "sensor trend history must cover the full day-sampling trend window");
static_assert(kNightSlowWindowStartHour >= 0 && kNightSlowWindowStartHour < 24,
              "night slow window start hour must be in 0..23");
static_assert(kNightSlowWindowEndHour >= 0 && kNightSlowWindowEndHour < 24,
              "night slow window end hour must be in 0..23");
static_assert(kSecondsPerHour > 0, "seconds per hour must be positive");

int seconds_until_next_interval(const struct tm &local, int interval_seconds)
{
    if (interval_seconds <= 0) {
        ESP_LOGW(TAG, SENSOR_INTERVAL_INVALID_LOG_FORMAT, interval_seconds);
        return kSecondsPerMinute;
    }
    int seconds_into_hour = local.tm_min * kSecondsPerMinute + local.tm_sec;
    int seconds_to_next = interval_seconds - (seconds_into_hour % interval_seconds);
    if (seconds_to_next <= 0 || seconds_to_next > interval_seconds) {
        seconds_to_next = interval_seconds;
    }
    return seconds_to_next;
}

TickType_t next_periodic_sample_tick(TickType_t now,
                                     int day_minutes,
                                     int night_minutes,
                                     int unknown_time_ms)
{
    struct tm local = {};
    if (!is_system_time_plausible(&local)) {
        return now + pdMS_TO_TICKS(unknown_time_ms);
    }
    int interval_seconds = periodic_sample_minutes(local, day_minutes, night_minutes) * kSecondsPerMinute;
    int seconds_to_next = seconds_until_next_interval(local, interval_seconds);
    return now + pdMS_TO_TICKS(seconds_to_next * kMsPerSecond);
}

bool should_log_nvs_read_error(esp_err_t err)
{
    return err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND;
}

bool is_hourly_meta_valid(const HourlySensorHistoryMeta &meta, size_t meta_len)
{
    return meta_len == sizeof(meta) &&
           meta.magic == kHourlyHistoryMagic &&
           meta.version == kHourlyHistoryMetaVersion &&
           meta.count == kHourlyHistoryCount;
}

bool is_legacy_hourly_history_valid(const LegacyHourlySensorHistoryBlob &legacy, size_t legacy_len)
{
    return legacy_len == sizeof(legacy) &&
           legacy.magic == kHourlyHistoryMagic &&
           legacy.version == kLegacyHourlyHistoryVersion &&
           legacy.count == kLegacyHourlyHistoryCount;
}

bool is_hourly_history_index_valid(int index)
{
    return index >= 0 && index < kHourlyHistoryCount;
}

int hourly_slot_index_for_time(time_t hour_start)
{
    int index = (int)((hour_start / kSecondsPerHour) % kHourlyHistoryCount);
    if (index < 0) {
        index += kHourlyHistoryCount;
    }
    return index;
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

bool sensor_sample_in_trend_window(const SensorSample &sample, int64_t now_ms)
{
    return sample.sampled_at_ms > 0 &&
           sample.sampled_at_ms >= now_ms - kSensorTrendWindowMs &&
           sample.sampled_at_ms <= now_ms;
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

SensorHistoryAverage calculate_sensor_history_average()
{
    SensorHistoryAverage average = {};
    if (g_sensor_history_count <= 0) {
        return average;
    }
    int64_t now_ms = sensor_trend_now_ms();
    float temp_sum = 0.0f;
    float humi_sum = 0.0f;
    for (int i = 0; i < g_sensor_history_count; ++i) {
        if (!sensor_sample_in_trend_window(g_sensor_history[i], now_ms)) {
            continue;
        }
        temp_sum += g_sensor_history[i].temperature;
        humi_sum += g_sensor_history[i].humidity;
        ++average.count;
    }
    if (average.count <= 0) {
        return average;
    }
    average.temperature = temp_sum / average.count;
    average.humidity = humi_sum / average.count;
    return average;
}

void update_sensor_trend_from_average(const SensorHistoryAverage &average)
{
    if (average.count <= 0) {
        g_temp_trend = 0;
        g_humi_trend = 0;
        g_sensor_average_valid = false;
        return;
    }
    if (g_sensor_average_valid && average.count >= 2) {
        float temp_delta = average.temperature - g_last_temp_average;
        float humi_delta = average.humidity - g_last_humi_average;
        g_temp_trend = temp_delta > kTrendEpsilon ? 1 : (temp_delta < -kTrendEpsilon ? -1 : 0);
        g_humi_trend = humi_delta > kTrendEpsilon ? 1 : (humi_delta < -kTrendEpsilon ? -1 : 0);
    } else {
        g_temp_trend = 0;
        g_humi_trend = 0;
    }
    g_last_temp_average = average.temperature;
    g_last_humi_average = average.humidity;
    g_sensor_average_valid = true;
}
} // namespace

static bool hourly_slot_key(int index, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    if (!is_hourly_history_index_valid(index)) {
        out[0] = '\0';
        ESP_LOGW(TAG, HOURLY_SLOT_KEY_INDEX_INVALID_LOG_FORMAT, index);
        return false;
    }
    int written = snprintf(out, out_len, kHourlySlotKeyFormat, index);
    if (written < 0 || (size_t)written >= out_len) {
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
    if (should_log_nvs_read_error(err)) {
        ESP_LOGW(TAG, HOURLY_SLOT_READ_FAILED_LOG_FORMAT, key, esp_err_to_name(err));
    }
    return false;
}

static esp_err_t save_hourly_sensor_meta_and_slot(nvs_handle_t nvs, int index)
{
    HourlySensorHistoryMeta meta = {};
    meta.last_saved_at = g_last_hourly_saved_at;
    esp_err_t err = nvs_set_blob(nvs, kHourlyHistoryMetaKey, &meta, sizeof(meta));
    if (err != ESP_OK) {
        return err;
    }

    char key[kHourlySlotKeyBufferSize] = {};
    if (!hourly_slot_key(index, key, sizeof(key))) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_blob(nvs, key, &g_hourly_history.samples[index], sizeof(g_hourly_history.samples[index]));
}

int boot_sync_remaining_ms()
{
    if (g_boot_sync_deadline_us <= 0) {
        return INT32_MAX;
    }
    int64_t remaining_us = g_boot_sync_deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return 0;
    }
    int64_t remaining_ms = remaining_us / kUsPerMs;
    return remaining_ms > INT32_MAX ? INT32_MAX : (int)remaining_ms;
}

bool is_system_time_plausible(struct tm *local_out)
{
    time_t now;
    time(&now);
    struct tm local = {};
    localtime_r(&now, &local);
    int year = local.tm_year + kTmYearOffset;
    if (local_out) {
        *local_out = local;
    }
    return year >= kMinValidYear && year <= kMaxValidYear;
}

bool is_tm_plausible(const struct tm &local)
{
    int year = local.tm_year + kTmYearOffset;
    return year >= kMinValidYear && year <= kMaxValidYear;
}

bool is_night_slow_window(const struct tm &local)
{
    return local.tm_hour >= kNightSlowWindowStartHour || local.tm_hour < kNightSlowWindowEndHour;
}

int periodic_sample_minutes(const struct tm &local, int day_minutes, int night_minutes)
{
    return is_night_slow_window(local) ? night_minutes : day_minutes;
}

time_t hour_start_from_time(time_t value)
{
    struct tm local = {};
    localtime_r(&value, &local);
    local.tm_min = 0;
    local.tm_sec = 0;
    return mktime(&local);
}

void reset_hourly_sensor_history()
{
    memset(&g_hourly_history, 0, sizeof(g_hourly_history));
    g_hourly_history.magic = kHourlyHistoryMagic;
    g_hourly_history.version = kLegacyHourlyHistoryVersion;
    g_hourly_history.count = kHourlyHistoryCount;
    g_last_hourly_saved_at = 0;
    ++g_hourly_history_version;
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
    if (err == ESP_OK && is_hourly_meta_valid(meta, meta_len)) {
        int loaded = 0;
        int64_t newest_slot = 0;
        for (int i = 0; i < kHourlyHistoryCount; ++i) {
            if (load_hourly_sensor_slot(nvs, i, &newest_slot)) {
                ++loaded;
            }
        }
        if (loaded > 0) {
            g_last_hourly_saved_at = newest_slot;
            if (meta.last_saved_at > g_last_hourly_saved_at) {
                g_last_hourly_saved_at = meta.last_saved_at;
            }
            nvs_close(nvs);
            ++g_hourly_history_version;
            return;
        }
    } else if (should_log_nvs_read_error(err)) {
        ESP_LOGW(TAG, HOURLY_META_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
    }

    LegacyHourlySensorHistoryBlob legacy = {};
    size_t legacy_len = sizeof(legacy);
    err = nvs_get_blob(nvs, kLegacyHourlyHistoryKey, &legacy, &legacy_len);
    nvs_close(nvs);
    if (err != ESP_OK || !is_legacy_hourly_history_valid(legacy, legacy_len)) {
        if (should_log_nvs_read_error(err)) {
            ESP_LOGW(TAG, LEGACY_HOURLY_HISTORY_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
        return;
    }
    store_legacy_hourly_history_samples(legacy);
    ++g_hourly_history_version;
}

static bool save_hourly_sensor_slot(int index)
{
    if (!is_hourly_history_index_valid(index)) {
        ESP_LOGW(TAG, HOURLY_SLOT_INDEX_INVALID_LOG_FORMAT, index);
        return false;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kSensorNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, SENSOR_NVS_OPEN_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return false;
    }
    err = save_hourly_sensor_meta_and_slot(nvs, index);
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
    if (hour_start <= 0 || hour_start == g_last_hourly_saved_at) {
        return;
    }
    int index = hourly_slot_index_for_time(hour_start);
    g_hourly_history.samples[index].timestamp = hour_start;
    g_hourly_history.samples[index].temperature = temp;
    g_hourly_history.samples[index].humidity = humi;
    g_hourly_history.samples[index].valid = 1;
    g_last_hourly_saved_at = hour_start;
    ++g_hourly_history_version;
    save_hourly_sensor_slot(index);
    notify_ui_task();
}

time_t next_weather_sync_time(time_t from)
{
    struct tm candidate = {};
    localtime_r(&from, &candidate);
    if (!is_tm_plausible(candidate)) {
        return from + kWeatherSyncFallbackSeconds;
    }
    candidate.tm_sec = 0;
    candidate.tm_min = 0;
    candidate.tm_hour += kWeatherSyncSearchStepHours;
    time_t next = mktime(&candidate);
    for (int i = 0; i < kWeatherSyncSearchHours; ++i) {
        struct tm local = {};
        localtime_r(&next, &local);
        if (!is_night_slow_window(local) || (local.tm_hour % 2 == 0)) {
            return next;
        }
        local.tm_hour += kWeatherSyncSearchStepHours;
        next = mktime(&local);
    }
    return from + kWeatherSyncFallbackSeconds;
}

void update_sensor_history(float temp, float humi)
{
    append_sensor_history_sample(temp, humi);
    update_sensor_trend_from_average(calculate_sensor_history_average());
}

void sample_sensor()
{
    float temp = 0.0f;
    float humi = 0.0f;
    g_sensor_ok = g_shtc3 && g_shtc3->Shtc3_ReadTempHumi(&temp, &humi) == 0;
    if (g_sensor_ok) {
        g_temperature = temp;
        g_humidity = humi;
        update_sensor_history(temp, humi);
        record_hourly_sensor_sample(temp, humi);
    }
}

TickType_t next_sensor_sample_tick(TickType_t now)
{
    return next_periodic_sample_tick(now,
                                     kSensorSampleDayMinutes,
                                     kSensorSampleNightMinutes,
                                     kUnknownTimeSensorSampleMs);
}

TickType_t next_battery_sample_tick(TickType_t now)
{
    TickType_t normal_sample = next_periodic_sample_tick(now,
                                                         kBatterySampleDayMinutes,
                                                         kBatterySampleNightMinutes,
                                                         kBatterySampleUnknownTimeMinutes * kSecondsPerMinute * kMsPerSecond);
    TickType_t charge_probe = now + pdMS_TO_TICKS(kBatteryChargeProbeSampleMs);
    return charge_probe < normal_sample ? charge_probe : normal_sample;
}
