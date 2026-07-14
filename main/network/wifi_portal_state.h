// 声明 Wi-Fi 门户跨任务运行与诊断状态的窄访问接口。
#pragma once

bool setup_portal_active_load();
void setup_portal_active_store(bool active);
int wifi_last_disconnect_reason();
void record_wifi_disconnect_reason(int reason);
void clear_wifi_last_disconnect_reason();
