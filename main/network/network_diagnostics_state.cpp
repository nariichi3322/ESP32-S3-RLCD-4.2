// 管理网络检测后台写入与 UI 读取之间的一致状态快照。
#include "network_diagnostics_state.h"

#include "scoped_semaphore_lock.h"

#include <esp_attr.h>

#include <atomic>
#include <cstring>

namespace {
StaticTaskMutex s_network_diag_mutex;
NetworkDiagState s_network_diag_state = kNetworkDiagIdle;
EXT_RAM_BSS_ATTR char s_network_diag_lines[kNetworkDiagLineCount]
                                           [kNetworkDiagLineLen] = {};
std::atomic<bool> s_network_diag_page_requested{false};

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
    return s_network_diag_mutex.init();
}

void network_diag_state_clear(NetworkDiagState state)
{
    ScopedSemaphoreLock lock(s_network_diag_mutex);
    if (!lock) {
        return;
    }
    s_network_diag_state = state;
    memset(s_network_diag_lines, 0, sizeof(s_network_diag_lines));
}

void network_diag_state_store(NetworkDiagState state)
{
    ScopedSemaphoreLock lock(s_network_diag_mutex);
    if (!lock) {
        return;
    }
    s_network_diag_state = state;
}

NetworkDiagState network_diag_state_load()
{
    ScopedSemaphoreLock lock(s_network_diag_mutex);
    if (!lock) {
        return kNetworkDiagIdle;
    }
    return s_network_diag_state;
}

void network_diag_line_store(int index, const char *text)
{
    if (!network_diag_line_index_valid(index)) {
        return;
    }
    ScopedSemaphoreLock lock(s_network_diag_mutex);
    if (!lock) {
        return;
    }
    copy_line(s_network_diag_lines[index], text);
}

void network_diag_snapshot_load(NetworkDiagnosticsSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    ScopedSemaphoreLock lock(s_network_diag_mutex);
    if (!lock) {
        return;
    }
    snapshot->state = s_network_diag_state;
    memcpy(snapshot->lines, s_network_diag_lines, sizeof(snapshot->lines));
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
