// 定义小智对话退出与说话期间唤醒打断的纯判定规则。
#pragma once

#include <stdint.h>
#include <string.h>

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

inline bool xiaozhi_user_requested_exit(const char *text)
{
    if (!text || text[0] == '\0') {
        return false;
    }
    constexpr const char *kExplicitExitPhrases[] = {
        "关闭小智",
        "停止小智",
        "退出小智",
        "结束小智",
        "关闭对话",
        "停止对话",
        "退出对话",
        "结束对话",
    };
    for (const char *phrase : kExplicitExitPhrases) {
        if (strstr(text, phrase)) {
            return true;
        }
    }
    constexpr const char *kStandaloneExitCommands[] = {
        "关闭", "关闭。", "关闭！", "关闭？",
        "停止", "停止。", "停止！", "停止？",
        "退出", "退出。", "退出！", "退出？",
        "结束", "结束。", "结束！", "结束？",
        "退下", "退下。", "退下！", "退下？",
        "退下吧", "退下吧。", "退下吧！", "退下吧？",
        "你退下吧", "你退下吧。", "你退下吧！", "你退下吧？",
    };
    // 单独的结束动词代表退出；带“闹钟”等宾语的命令继续交给 MCP。
    for (const char *command : kStandaloneExitCommands) {
        if (strcmp(text, command) == 0) {
            return true;
        }
    }
    return false;
}
