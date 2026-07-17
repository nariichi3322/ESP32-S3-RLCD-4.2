// 验证按键任务只在空闲工作页、无按压且 GPIO 唤醒可用时进入事件等待。
#include "input_button_wait_policy.h"

#include <assert.h>

int main()
{
    const ButtonIdleContext idle_work_page = {
        false, false, false, false, false, false, false, false, false, true,
    };
    const ButtonIdleContext low_battery_page = {
        false, false, false, false, false, false, false, false, true, false,
    };
    assert(button_idle_work_page_context(idle_work_page));
    assert(button_idle_work_page_context(low_battery_page));

    ButtonIdleContext blocked = idle_work_page;
    blocked.battery_charging = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.setup_portal_active = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.settings_requested = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.info_requested = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.network_diag_requested = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.ota_active = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.audio_playing = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.wifi_on = true;
    assert(!button_idle_work_page_context(blocked));
    blocked = idle_work_page;
    blocked.active_page_enabled = false;
    assert(!button_idle_work_page_context(blocked));

    assert(button_task_can_wait_for_edge(true, true, false, false, false));
    assert(!button_task_can_wait_for_edge(false, true, false, false, false));
    assert(!button_task_can_wait_for_edge(true, false, false, false, false));
    assert(!button_task_can_wait_for_edge(true, true, true, false, true));
    assert(!button_task_can_wait_for_edge(true, true, false, true, true));
    assert(!button_task_can_wait_for_edge(true, true, false, false, true));
    return 0;
}
