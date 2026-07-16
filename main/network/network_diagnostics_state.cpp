// 管理网络检测后台写入与 UI 读取之间的一致状态快照。
#include "network_diagnostics_state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstring>

namespace {
StaticSemaphore_t s_network_diag_mutex_storage = {};
SemaphoreHandle_t s_network_diag_mutex = nullptr;
int s_network_diag_state = 0;
char s_network_diag_lines[kNetworkDiagLineCount][kNetworkDiagLineLen] = {};
std::atomic<bool> s_network_diag_page_requested{false};

bool lock_network_diag_state()
{
    return s_network_diag_mutex &&
           xSemaphoreTake(s_network_diag_mutex, portMAX_DELAY) == pdTRUE;
}

void unlock_network_diag_state()
{
    xSemaphoreGive(s_network_diag_mutex);
}

void copy_line(char *out, const char *text)
{
    memset(out, 0, kNetworkDiagLineLen);
    if (!text) {
        return;
    }
    size_t length = strnlen(text, kNetworkDiagLineLen - 1);
    memcpy(out, text, length);
}
} // namespace

bool network_diagnostics_state_init()
{
    if (s_network_diag_mutex) {
        return true;
    }
    s_network_diag_mutex =
        xSemaphoreCreateMutexStatic(&s_network_diag_mutex_storage);
    return s_network_diag_mutex != nullptr;
}

void network_diag_state_clear(int state)
{
    if (!lock_network_diag_state()) {
        return;
    }
    s_network_diag_state = state;
    memset(s_network_diag_lines, 0, sizeof(s_network_diag_lines));
    unlock_network_diag_state();
}

void network_diag_state_store(int state)
{
    if (!lock_network_diag_state()) {
        return;
    }
    s_network_diag_state = state;
    unlock_network_diag_state();
}

int network_diag_state_load()
{
    if (!lock_network_diag_state()) {
        return 0;
    }
    int state = s_network_diag_state;
    unlock_network_diag_state();
    return state;
}

void network_diag_line_store(int index, const char *text)
{
    if (!network_diag_line_index_valid(index)) {
        return;
    }
    if (!lock_network_diag_state()) {
        return;
    }
    copy_line(s_network_diag_lines[index], text);
    unlock_network_diag_state();
}

void network_diag_snapshot_load(NetworkDiagnosticsSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!lock_network_diag_state()) {
        return;
    }
    snapshot->state = s_network_diag_state;
    memcpy(snapshot->lines, s_network_diag_lines, sizeof(snapshot->lines));
    unlock_network_diag_state();
}

bool network_diag_page_requested()
{
    return s_network_diag_page_requested.load(std::memory_order_acquire);
}

void network_diag_page_request()
{
    s_network_diag_page_requested.store(true, std::memory_order_release);
}

void network_diag_page_clear()
{
    s_network_diag_page_requested.store(false, std::memory_order_release);
}
