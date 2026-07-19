// 声明网络检测会话、逐行结果更新和检测执行入口。
#pragma once

void network_diag_reset();
void network_diag_begin();
void network_diag_finish();
void network_diag_set_line(int index, const char *fmt, ...);

// 只执行检测项目；调用方负责 begin/finish 与 Wi-Fi 会话生命周期。
void run_network_diagnostic_checks();
