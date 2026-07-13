// 验证小智 TTS 全程不会因 WakeNet/扬声器回声误触发而中断回复。
#include "xiaozhi_conversation_policy.h"

#include <cassert>

int main()
{
    assert(!xiaozhi_wake_interrupt_allowed(false, false, true, 5000));
    assert(!xiaozhi_wake_interrupt_allowed(true, false, false, 5000));
    assert(!xiaozhi_wake_interrupt_allowed(true,
                                           false,
                                           true,
                                           kXiaozhiWakeInterruptArmDelayMs - 1));
    assert(!xiaozhi_wake_interrupt_allowed(true,
                                           false,
                                           true,
                                           kXiaozhiWakeInterruptArmDelayMs));
    assert(!xiaozhi_wake_interrupt_allowed(true, false, true, 5000));
    assert(!xiaozhi_wake_interrupt_allowed(true, true, true, 5000));
    return 0;
}
