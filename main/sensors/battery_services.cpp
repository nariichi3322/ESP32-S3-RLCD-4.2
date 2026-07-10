// 负责电池电压采样、电量估算和充电状态判断。
#include "sensor_services.h"

#include "ui_views.h"

#define BATTERY_ADC_CALIBRATION_RELEASE_FAILED_LOG_FORMAT "battery adc calibration release failed: %s"
#define BATTERY_ADC_UNIT_RELEASE_FAILED_LOG_FORMAT "battery adc unit release failed: %s"
#define BATTERY_ADC_READY_WITHOUT_HANDLE_LOG_FORMAT "battery adc marked ready without handle, resetting"
#define BATTERY_ADC_INIT_FAILED_LOG_FORMAT "battery adc init failed: %s"
#define BATTERY_ADC_CHANNEL_CONFIG_FAILED_LOG_FORMAT "battery adc channel config failed: %s"
#define BATTERY_ADC_CALIBRATION_UNAVAILABLE_LOG_FORMAT "battery adc calibration unavailable: %s"
#define BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT "battery percent output is null"
#define BATTERY_ADC_READ_FAILED_LOG_FORMAT "battery adc read failed: %s"
#define BATTERY_ADC_CALIBRATION_READ_FAILED_LOG_FORMAT "battery adc calibration read failed: %s"
#define BATTERY_ADC_SAMPLE_LOG_FORMAT "battery adc raw=%d adc_mv=%d battery=%.3fV soc=%d%%"

static constexpr const char *const kBatteryLogTexts[] = {
    BATTERY_ADC_CALIBRATION_RELEASE_FAILED_LOG_FORMAT,
    BATTERY_ADC_UNIT_RELEASE_FAILED_LOG_FORMAT,
    BATTERY_ADC_READY_WITHOUT_HANDLE_LOG_FORMAT,
    BATTERY_ADC_INIT_FAILED_LOG_FORMAT,
    BATTERY_ADC_CHANNEL_CONFIG_FAILED_LOG_FORMAT,
    BATTERY_ADC_CALIBRATION_UNAVAILABLE_LOG_FORMAT,
    BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT,
    BATTERY_ADC_READ_FAILED_LOG_FORMAT,
    BATTERY_ADC_CALIBRATION_READ_FAILED_LOG_FORMAT,
    BATTERY_ADC_SAMPLE_LOG_FORMAT,
};
static constexpr float kBatteryVoltageDivider = 3.0f;
static constexpr float kBatteryMillivoltsToVolts = 0.001f;
static constexpr float kBatteryEmptyVoltage = 3.00f;
static constexpr float kBatteryFullVoltage = 4.12f;
static constexpr float kBatteryVoltageRange = kBatteryFullVoltage - kBatteryEmptyVoltage;
static constexpr float kBatteryPercentScale = 100.0f;
static constexpr float kBatteryPercentRoundOffset = 0.5f;
static constexpr float kBatteryValidPreviousVoltageMin = 0.0f;
static constexpr adc_unit_t kBatteryAdcUnit = ADC_UNIT_1;
static constexpr adc_channel_t kBatteryAdcChannel = ADC_CHANNEL_3;
static constexpr adc_bitwidth_t kBatteryAdcBitwidth = ADC_BITWIDTH_12;
static constexpr adc_atten_t kBatteryAdcAtten = ADC_ATTEN_DB_12;
static constexpr int kBatteryAdcReferenceMv = 3300;
static constexpr int kBatteryAdcRawMax = 4095;
static constexpr int kBatteryPercentMin = 0;
static constexpr int kBatteryPercentMax = 100;
static constexpr int kBatteryPercentUnknown = -1;
static constexpr int kTmYearOffset = 1900;
static constexpr int kBatteryMinValidYear = 2023;
static constexpr int kBatteryMinValidTmYear = kBatteryMinValidYear - kTmYearOffset;

static constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

template <typename T, size_t N>
static constexpr size_t array_count(const T (&)[N])
{
    return N;
}

template <typename T, size_t N>
static constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *item : items) {
        if (!cstr_nonempty(item)) {
            return false;
        }
    }
    return true;
}

static_assert(kBatteryVoltageDivider > 0.0f, "battery voltage divider must be positive");
static_assert(kBatteryMillivoltsToVolts > 0.0f, "millivolts-to-volts scale must be positive");
static_assert(kBatteryFullVoltage > kBatteryEmptyVoltage, "battery voltage range must be positive");
static_assert(kBatteryVoltageRange > 0.0f, "battery voltage range must be positive");
static_assert(kBatteryPercentScale > 0.0f, "battery percent scale must be positive");
static_assert(kBatteryPercentRoundOffset >= 0.0f && kBatteryPercentRoundOffset < 1.0f,
              "battery percent rounding offset must stay within one percent step");
static_assert(kBatteryValidPreviousVoltageMin <= kBatteryEmptyVoltage,
              "battery previous-voltage sentinel must not exceed empty voltage");
static_assert(kBatteryAdcReferenceMv > 0, "battery ADC reference voltage must be positive");
static_assert(kBatteryAdcRawMax > 0, "battery ADC raw max must be positive");
static_assert(kBatteryAdcRawMax == (1 << 12) - 1, "12-bit battery ADC raw max must stay 4095");
static_assert(kBatteryPercentMin == 0, "battery percent min is expected to be zero");
static_assert(kBatteryPercentMax == 100, "battery percent max is expected to be one hundred");
static_assert(kBatteryPercentMin < kBatteryPercentMax, "battery percent range must be ordered");
static_assert(kBatteryPercentUnknown < kBatteryPercentMin, "unknown battery percent must be below valid range");
static_assert(kTmYearOffset > 0, "tm year offset must be positive");
static_assert(kBatteryMinValidYear > kTmYearOffset, "battery valid year must exceed tm year offset");
static_assert(kBatteryMinValidTmYear >= 0, "battery valid time year floor must be non-negative");
static_assert(kBatteryChargingRiseSamples > 0, "charging detection must require at least one rising sample");
static_assert(kBatteryChargingStopSamples > 0,
              "charging stop detection must require at least one confirming sample");
static_assert(kBatteryChargingRiseVoltage > kBatteryChargingStopVoltage,
              "charging rise threshold must stay above stop threshold");
static_assert(array_count(kBatteryLogTexts) > 0,
              "battery log guard must cover battery log texts");
static_assert(cstr_array_nonempty(kBatteryLogTexts), "battery service log texts must be non-empty");

static int clamp_battery_percent(int percent)
{
    if (percent < kBatteryPercentMin) {
        return kBatteryPercentMin;
    }
    if (percent > kBatteryPercentMax) {
        return kBatteryPercentMax;
    }
    return percent;
}

static bool previous_battery_voltage_valid(float voltage)
{
    return voltage >= kBatteryValidPreviousVoltageMin;
}

static bool previous_battery_percent_valid(int percent)
{
    return percent >= kBatteryPercentMin;
}

static bool battery_percent_increased(int previous_percent, int current_percent)
{
    return previous_battery_percent_valid(previous_percent) && current_percent > previous_percent;
}

static bool battery_voltage_dropped_from_peak(float voltage, float peak_voltage)
{
    return peak_voltage > 0.0f && voltage <= peak_voltage - kBatteryChargingStopVoltage;
}

static bool charging_should_stop(float voltage, float peak_voltage, int stop_samples)
{
    return battery_voltage_dropped_from_peak(voltage, peak_voltage) &&
           stop_samples >= kBatteryChargingStopSamples;
}

static bool battery_time_valid(time_t value)
{
    if (value <= 0) {
        return false;
    }
    struct tm local = {};
    return localtime_r(&value, &local) && local.tm_year >= kBatteryMinValidTmYear;
}

static void update_last_charge_time_if_needed()
{
    time_t now = 0;
    time(&now);
    if (battery_time_valid(now)) {
        g_last_charge_time = now;
    }
}

void release_battery_gauge()
{
    if (g_battery_adc_cali_ready && g_battery_adc_cali) {
        esp_err_t err = adc_cali_delete_scheme_curve_fitting(g_battery_adc_cali);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_RELEASE_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }
    g_battery_adc_cali = nullptr;
    g_battery_adc_cali_ready = false;

    if (g_battery_adc) {
        esp_err_t err = adc_oneshot_del_unit(g_battery_adc);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_UNIT_RELEASE_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }
    g_battery_adc = nullptr;
    g_battery_adc_ready = false;
}

bool init_battery_gauge()
{
    if (g_battery_adc_ready) {
        if (!g_battery_adc) {
            ESP_LOGW(TAG, BATTERY_ADC_READY_WITHOUT_HANDLE_LOG_FORMAT);
            release_battery_gauge();
            return false;
        }
        return true;
    }

    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = kBatteryAdcUnit;
    esp_err_t err = adc_oneshot_new_unit(&init_config, &g_battery_adc);
    if (err != ESP_OK) {
        g_battery_adc = nullptr;
        ESP_LOGW(TAG, BATTERY_ADC_INIT_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_config = {};
    chan_config.bitwidth = kBatteryAdcBitwidth;
    chan_config.atten = kBatteryAdcAtten;
    err = adc_oneshot_config_channel(g_battery_adc, kBatteryAdcChannel, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, BATTERY_ADC_CHANNEL_CONFIG_FAILED_LOG_FORMAT, esp_err_to_name(err));
        release_battery_gauge();
        return false;
    }

    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = kBatteryAdcUnit;
    cali_config.chan = kBatteryAdcChannel;
    cali_config.atten = kBatteryAdcAtten;
    cali_config.bitwidth = kBatteryAdcBitwidth;
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &g_battery_adc_cali);
    if (err == ESP_OK) {
        g_battery_adc_cali_ready = true;
    } else {
        ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_UNAVAILABLE_LOG_FORMAT, esp_err_to_name(err));
    }

    g_battery_adc_ready = true;
    return true;
}

int battery_percent_from_voltage(float voltage)
{
    int percent = (int)(((voltage - kBatteryEmptyVoltage) * kBatteryPercentScale /
                         kBatteryVoltageRange) + kBatteryPercentRoundOffset);
    return clamp_battery_percent(percent);
}

int battery_adc_raw_to_mv(int raw)
{
    return (raw * kBatteryAdcReferenceMv) / kBatteryAdcRawMax;
}

float battery_voltage_from_adc_mv(int adc_mv)
{
    return adc_mv * kBatteryMillivoltsToVolts * kBatteryVoltageDivider;
}

bool read_battery_percent(int *percent)
{
    if (!percent) {
        ESP_LOGW(TAG, BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT);
        return false;
    }
    if (!init_battery_gauge()) {
        return false;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(g_battery_adc, kBatteryAdcChannel, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, BATTERY_ADC_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        release_battery_gauge();
        return false;
    }

    int adc_mv = battery_adc_raw_to_mv(raw);
    if (g_battery_adc_cali_ready) {
        err = adc_cali_raw_to_voltage(g_battery_adc_cali, raw, &adc_mv);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }

    float voltage = battery_voltage_from_adc_mv(adc_mv);
    int soc = battery_percent_from_voltage(voltage);
    ESP_LOGI(TAG, BATTERY_ADC_SAMPLE_LOG_FORMAT, raw, adc_mv, voltage, soc);
    *percent = soc;
    g_battery_voltage = voltage;
    return true;
}

static void update_battery_charging_state(int previous_percent,
                                          float previous_voltage,
                                          int current_percent,
                                          int *charging_rise_samples,
                                          int *charging_stop_samples,
                                          float *charging_peak_voltage)
{
    if (!charging_rise_samples || !charging_stop_samples || !charging_peak_voltage) {
        return;
    }
    if (!previous_battery_voltage_valid(previous_voltage)) {
        *charging_rise_samples = 0;
        *charging_stop_samples = 0;
        *charging_peak_voltage = 0.0f;
        return;
    }

    float delta = g_battery_voltage - previous_voltage;
    bool charging_activity = delta >= kBatteryChargingRiseVoltage ||
                             battery_percent_increased(previous_percent, current_percent);

    if (charging_activity) {
        if (*charging_rise_samples < kBatteryChargingRiseSamples) {
            ++(*charging_rise_samples);
        }
    } else {
        *charging_rise_samples = 0;
    }

    if (g_battery_charging) {
        if (g_battery_voltage > *charging_peak_voltage) {
            *charging_peak_voltage = g_battery_voltage;
            *charging_stop_samples = 0;
        } else if (battery_voltage_dropped_from_peak(g_battery_voltage, *charging_peak_voltage)) {
            if (*charging_stop_samples < kBatteryChargingStopSamples) {
                ++(*charging_stop_samples);
            }
        } else {
            *charging_stop_samples = 0;
        }

        if (charging_should_stop(g_battery_voltage, *charging_peak_voltage, *charging_stop_samples)) {
            g_battery_charging = false;
            *charging_rise_samples = 0;
            *charging_stop_samples = 0;
            *charging_peak_voltage = 0.0f;
        }
    } else if (*charging_rise_samples >= kBatteryChargingRiseSamples) {
        g_battery_charging = true;
        *charging_stop_samples = 0;
        *charging_peak_voltage = g_battery_voltage;
    }
}

static void reset_battery_state_after_sample_failure(int *charging_rise_samples,
                                                      int *charging_stop_samples,
                                                      float *charging_peak_voltage)
{
    g_battery_percent = kBatteryPercentUnknown;
    g_battery_charging = false;
    if (charging_rise_samples) {
        *charging_rise_samples = 0;
    }
    if (charging_stop_samples) {
        *charging_stop_samples = 0;
    }
    if (charging_peak_voltage) {
        *charging_peak_voltage = 0.0f;
    }
}

static void publish_battery_sample_update(bool force)
{
    bool low_battery_changed = update_low_battery_state();
    if (force || low_battery_changed) {
        ++g_battery_version;
        notify_ui_task();
    }
}

void sample_battery()
{
    static int charging_rise_samples = 0;
    static int charging_stop_samples = 0;
    static float charging_peak_voltage = 0.0f;
    int percent = kBatteryPercentUnknown;
    bool previous_charging = g_battery_charging;
    int previous_percent = g_battery_percent;
    float previous_voltage = g_battery_voltage;
    if (read_battery_percent(&percent)) {
        g_battery_percent = percent;
        update_battery_charging_state(previous_percent,
                                      previous_voltage,
                                      percent,
                                      &charging_rise_samples,
                                      &charging_stop_samples,
                                      &charging_peak_voltage);
        if (previous_charging && !g_battery_charging) {
            update_last_charge_time_if_needed();
        }
        release_battery_gauge();
    } else {
        reset_battery_state_after_sample_failure(&charging_rise_samples,
                                                 &charging_stop_samples,
                                                 &charging_peak_voltage);
    }
    bool sample_changed = previous_percent != g_battery_percent ||
                          previous_charging != g_battery_charging;
    publish_battery_sample_update(sample_changed);
}
