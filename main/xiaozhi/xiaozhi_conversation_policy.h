// 定义小智说话期间唤醒打断的纯判定规则，隔离扬声器回声误触发。
#pragma once

#include <stdint.h>

// 当前硬件没有稳定到足以区分近场用户唤醒与扬声器回声的置信度。
// TTS 期间禁用 WakeNet 打断，避免误触发 abort 后截断回复及尚未完成的 MCP。
inline constexpr bool kXiaozhiWakeInterruptDuringTtsEnabled = false;
inline constexpr uint32_t kXiaozhiWakeInterruptArmDelayMs = 1000;

constexpr bool xiaozhi_wake_interrupt_allowed(bool server_speaking,
                                              bool tts_stop_pending,
                                              bool tts_start_known,
                                              uint32_t speaking_elapsed_ms)
{
    return kXiaozhiWakeInterruptDuringTtsEnabled &&
           server_speaking &&
           !tts_stop_pending &&
           tts_start_known &&
           speaking_elapsed_ms >= kXiaozhiWakeInterruptArmDelayMs;
}
