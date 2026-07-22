// 使用静态任务互斥管理完整 OTA 快照，并用原子镜像提供高频状态判断。
#include "ota_runtime_state.h"

#include "network_runtime_events.h"
#include "ota_flow_policy.h"
#include "scoped_semaphore_lock.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t kOtaRuntimeStateMask = 0xffu;
constexpr uint32_t kOtaRuntimeRebootPendingBit = 1u << 8;

constexpr uint32_t pack_ota_runtime_flags(int state, bool reboot_pending)
{
    return (static_cast<uint32_t>(state) & kOtaRuntimeStateMask) |
           (reboot_pending ? kOtaRuntimeRebootPendingBit : 0u);
}

StaticTaskMutex s_ota_runtime_mutex;
OtaRuntimeSnapshot s_ota_runtime = {
    kOtaIdle,
    -1,
    -1,
    0,
    false,
    false,
    "BOOT: Check Update",
};
std::atomic<uint32_t> s_ota_runtime_flags{
    pack_ota_runtime_flags(kOtaIdle, false),
};

static_assert(kOtaNoUpdate <= static_cast<int>(kOtaRuntimeStateMask),
              "OTA runtime state must fit the atomic mirror");

void copy_status(char *out, const char *status)
{
    memset(out, 0, kOtaStatusLen);
    if (!status) {
        return;
    }
    size_t length = strnlen(status, kOtaStatusLen - 1);
    memcpy(out, status, length);
}

void notify_background_network_block_changed(int previous_state,
                                             int current_state)
{
    if (ota_background_network_block_changed(previous_state, current_state)) {
        notify_network_sync_runtime_state_changed();
    }
}
} // namespace

bool ota_runtime_state_init()
{
    return s_ota_runtime_mutex.init();
}

void ota_runtime_snapshot_load(OtaRuntimeSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    ScopedSemaphoreLock lock(s_ota_runtime_mutex.handle());
    if (!lock) {
        return;
    }
    *snapshot = s_ota_runtime;
}

int ota_runtime_state_load()
{
    uint32_t flags = s_ota_runtime_flags.load(std::memory_order_acquire);
    return static_cast<int>(flags & kOtaRuntimeStateMask);
}

bool ota_runtime_reboot_pending_load()
{
    uint32_t flags = s_ota_runtime_flags.load(std::memory_order_acquire);
    return (flags & kOtaRuntimeRebootPendingBit) != 0;
}

void ota_runtime_publish_status(int state,
                                const char *status,
                                int progress,
                                TickType_t status_until_tick,
                                bool status_hold_set)
{
    char prepared_status[kOtaStatusLen] = {};
    copy_status(prepared_status, status);

    int previous_state = kOtaIdle;
    {
        ScopedSemaphoreLock lock(s_ota_runtime_mutex.handle());
        if (!lock) {
            return;
        }
        previous_state = s_ota_runtime.state;
        s_ota_runtime.reboot_pending = false;
        s_ota_runtime.state = state;
        s_ota_runtime.progress = progress;
        memcpy(s_ota_runtime.status, prepared_status, sizeof(prepared_status));
        s_ota_runtime.status_hold_set = status_hold_set;
        s_ota_runtime.status_until_tick = status_hold_set ? status_until_tick : 0;
        s_ota_runtime_flags.store(pack_ota_runtime_flags(state, false),
                                  std::memory_order_release);
    }
    notify_background_network_block_changed(previous_state, state);
}

void ota_runtime_publish_download_status(const char *status,
                                         int progress,
                                         int speed_kbps)
{
    char prepared_status[kOtaStatusLen] = {};
    copy_status(prepared_status, status);

    int previous_state = kOtaIdle;
    {
        ScopedSemaphoreLock lock(s_ota_runtime_mutex.handle());
        if (!lock) {
            return;
        }
        previous_state = s_ota_runtime.state;
        s_ota_runtime.reboot_pending = false;
        s_ota_runtime.state = kOtaUpdating;
        s_ota_runtime.progress = progress;
        s_ota_runtime.speed_kbps = speed_kbps;
        memcpy(s_ota_runtime.status, prepared_status, sizeof(prepared_status));
        s_ota_runtime.status_hold_set = false;
        s_ota_runtime.status_until_tick = 0;
        s_ota_runtime_flags.store(pack_ota_runtime_flags(kOtaUpdating, false),
                                  std::memory_order_release);
    }
    notify_background_network_block_changed(previous_state, kOtaUpdating);
}

void ota_runtime_reboot_pending_store(bool pending)
{
    {
        ScopedSemaphoreLock lock(s_ota_runtime_mutex.handle());
        if (!lock) {
            return;
        }
        s_ota_runtime.reboot_pending = pending;
        const int state = s_ota_runtime.state;
        s_ota_runtime_flags.store(pack_ota_runtime_flags(state, pending),
                                  std::memory_order_release);
    }
}

void ota_runtime_reset_status_if_idle(TickType_t now, const char *idle_status)
{
    char prepared_idle_status[kOtaStatusLen] = {};
    copy_status(prepared_idle_status, idle_status);

    int previous_state = kOtaIdle;
    int current_state = kOtaIdle;
    {
        ScopedSemaphoreLock lock(s_ota_runtime_mutex.handle());
        if (!lock) {
            return;
        }
        previous_state = s_ota_runtime.state;
        if (ota_status_should_reset_to_idle(s_ota_runtime.state,
                                            s_ota_runtime.status_hold_set,
                                            now,
                                            s_ota_runtime.status_until_tick)) {
            s_ota_runtime.state = kOtaIdle;
            s_ota_runtime.status_hold_set = false;
            s_ota_runtime.status_until_tick = 0;
        }
        if (s_ota_runtime.state == kOtaIdle) {
            memcpy(s_ota_runtime.status,
                   prepared_idle_status,
                   sizeof(prepared_idle_status));
            s_ota_runtime.progress = -1;
            s_ota_runtime.speed_kbps = -1;
        }
        current_state = s_ota_runtime.state;
        const bool reboot_pending = s_ota_runtime.reboot_pending;
        s_ota_runtime_flags.store(pack_ota_runtime_flags(current_state,
                                                         reboot_pending),
                                  std::memory_order_release);
    }
    notify_background_network_block_changed(previous_state, current_state);
}
