#pragma once

#include "app_tick_time.h"

#include <stdint.h>

inline constexpr uint32_t kCodexBleGracefulStopMs = 60000U;

enum class CodexBleLifecycleAction : uint8_t {
    kWait,
    kStart,
    kStop,
};

template <typename Tick>
constexpr CodexBleLifecycleAction codex_ble_lifecycle_action(
    bool initialized,
    bool running,
    bool desired,
    bool graceful_stop_pending,
    Tick now,
    Tick graceful_stop_deadline)
{
    // An initialized transport without a running host is either stopping or
    // left behind by a failed deinit.  It must reach a fully deinitialized
    // state before a new host task can be started.
    if (initialized && !running) {
        return CodexBleLifecycleAction::kStop;
    }
    if (!desired && (initialized || running)) {
        if (!graceful_stop_pending ||
            app_tick_deadline_reached(now, graceful_stop_deadline)) {
            return CodexBleLifecycleAction::kStop;
        }
        return CodexBleLifecycleAction::kWait;
    }
    if (desired && !running) {
        return CodexBleLifecycleAction::kStart;
    }
    return CodexBleLifecycleAction::kWait;
}

// Preserve the pre-grace API's immediate-stop policy for host callers and
// cleanup paths that only model enabled/disabled state.
constexpr CodexBleLifecycleAction codex_ble_lifecycle_action(bool initialized,
                                                             bool running,
                                                             bool desired)
{
    return codex_ble_lifecycle_action(initialized,
                                      running,
                                      desired,
                                      false,
                                      uint32_t{0},
                                      uint32_t{0});
}

template <typename Tick>
constexpr Tick codex_ble_lifecycle_wait_ticks(bool desired,
                                              bool graceful_stop_pending,
                                              Tick now,
                                              Tick graceful_stop_deadline)
{
    if (desired || !graceful_stop_pending) {
        return static_cast<Tick>(0);
    }
    return app_tick_deadline_remaining(now, graceful_stop_deadline);
}

constexpr uint32_t codex_ble_transport_retry_delay_ms(uint32_t attempt)
{
    return attempt == 0 ? 1000U
         : attempt == 1 ? 5000U
         : attempt == 2 ? 15000U
                        : 30000U;
}
