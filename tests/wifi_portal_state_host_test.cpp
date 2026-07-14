// 验证配网页活跃状态和 Wi-Fi 断线原因的跨任务快照。
#include "wifi_portal_state.h"

#include <cassert>
#include <cstdio>

int main()
{
    assert(!setup_portal_active_load());
    setup_portal_active_store(true);
    assert(setup_portal_active_load());
    setup_portal_active_store(false);
    assert(!setup_portal_active_load());

    assert(wifi_last_disconnect_reason() == 0);
    record_wifi_disconnect_reason(8);
    assert(wifi_last_disconnect_reason() == 8);
    record_wifi_disconnect_reason(-1);
    assert(wifi_last_disconnect_reason() == -1);
    clear_wifi_last_disconnect_reason();
    assert(wifi_last_disconnect_reason() == 0);

    std::puts("Wi-Fi portal state host tests passed");
    return 0;
}
