// 统一定义低电量、充电识别、动画停止和充电快速采样策略。
#pragma once

inline constexpr int kLowBatteryEnterPercent = 10;
inline constexpr int kLowBatteryExitPercent = 13;
inline constexpr float kBatteryChargingRiseVoltage = 0.035f;
inline constexpr float kBatteryChargingStopVoltage = 0.006f;
inline constexpr int kBatteryChargingRiseSamples = 1;
inline constexpr int kBatteryChargingStopSamples = 5;
inline constexpr int kBatteryChargingAnimationStopPercent = 96;
inline constexpr int kBatteryChargingAnimationIdleMs = 10 * 60 * 1000;
inline constexpr int kBatteryChargingSampleMs = 1000;

constexpr bool battery_charging_requires_fast_sampling(bool charging)
{
    return charging;
}

static_assert(kLowBatteryEnterPercent >= 0,
              "low-battery entry threshold must be non-negative");
static_assert(kLowBatteryExitPercent > kLowBatteryEnterPercent,
              "low-battery exit threshold must exceed entry threshold");
static_assert(kBatteryChargingRiseVoltage > kBatteryChargingStopVoltage,
              "charging rise threshold must exceed stop threshold");
static_assert(kBatteryChargingRiseSamples > 0,
              "charging detection must require a rising sample");
static_assert(kBatteryChargingStopSamples > 0,
              "charging clear must require a confirming sample");
static_assert(kBatteryChargingAnimationStopPercent > kLowBatteryExitPercent &&
                  kBatteryChargingAnimationStopPercent <= 100,
              "charging animation stop threshold must be a valid percentage");
static_assert(kBatteryChargingAnimationIdleMs > 0,
              "charging animation idle timeout must be positive");
static_assert(kBatteryChargingSampleMs > 0,
              "charging sample interval must be positive");
