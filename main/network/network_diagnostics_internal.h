// 声明网络调度内部使用的检测会话和检测执行入口。
#pragma once

#include "network_diagnostics.h"

void network_diag_begin();
void network_diag_finish();
void network_diag_finish_with_status(const char *status_text);
void network_diag_finish_unavailable(const char *ip_location_text);

// 只执行检测项目；调用方负责 begin/finish 与 Wi-Fi 会话生命周期。
// 运行态阻断时返回 false，由调用方保留请求并关闭本轮射频。
bool run_network_diagnostic_checks();
