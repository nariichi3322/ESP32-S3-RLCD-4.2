// 验证任意页面在无按压且 GPIO 唤醒可用时进入事件等待。
#include "input_button_wait_policy.h"

#include <assert.h>

int main()
{
    assert(button_task_can_wait_for_edge(true, false, false, false));
    assert(!button_task_can_wait_for_edge(false, false, false, false));
    assert(!button_task_can_wait_for_edge(true, true, false, true));
    assert(!button_task_can_wait_for_edge(true, false, true, true));
    assert(!button_task_can_wait_for_edge(true, false, false, true));
    return 0;
}
