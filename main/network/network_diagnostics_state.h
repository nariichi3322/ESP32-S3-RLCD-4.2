// 提供网络检测页面请求、运行状态与逐行文本的线程安全接口。
#pragma once

#include "network_diagnostics_catalog.h"

enum NetworkDiagState : int {
    kNetworkDiagIdle = 0,
    kNetworkDiagRunning = 1,
    kNetworkDiagDone = 2,
};

struct NetworkDiagnosticsSnapshot {
    NetworkDiagState state = kNetworkDiagIdle;
    char lines[kNetworkDiagLineCount][kNetworkDiagLineLen] = {};
};

bool network_diagnostics_state_init();
void network_diag_state_clear(NetworkDiagState state);
void network_diag_state_store(NetworkDiagState state);
NetworkDiagState network_diag_state_load();
void network_diag_line_store(int index, const char *text);
void network_diag_snapshot_load(NetworkDiagnosticsSnapshot *snapshot);
bool network_diag_page_requested();
void network_diag_page_request();
void network_diag_page_clear();

static_assert(kNetworkDiagIdle == 0,
              "network diagnostics idle state must remain the zero default");
static_assert(kNetworkDiagDone == kNetworkDiagRunning + 1,
              "network diagnostics states must remain contiguous");
