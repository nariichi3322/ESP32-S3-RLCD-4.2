// 声明网络、音频与 USB 烧录唤醒电源管理锁的初始化、状态和嵌套所有权接口。
#pragma once

#include "freertos/FreeRTOS.h"

struct PowerLockDepthSnapshot {
    int network = 0;
    int audio = 0;
    int audio_wake = 0;
    int audio_cpu = 0;
    int usb_programming = 0;
};

[[nodiscard]] bool acquire_network_awake_lock();
void release_network_awake_lock();
bool network_awake_lock_active();
bool get_power_lock_depth_snapshot(PowerLockDepthSnapshot *out);
[[nodiscard]] bool acquire_audio_awake_lock();
void release_audio_awake_lock();

[[nodiscard]] bool request_usb_programming_wake_window(TickType_t now);
void service_usb_programming_wake_window(TickType_t now);
bool usb_programming_wake_window_active(TickType_t now);
TickType_t usb_programming_wake_window_remaining_ticks(TickType_t now);
