#pragma once

#include <stdint.h>

enum class CodexBleLifecycleAction : uint8_t {
    kWait,
    kStart,
    kStop,
};

constexpr CodexBleLifecycleAction codex_ble_lifecycle_action(bool initialized,
                                                             bool running,
                                                             bool desired)
{
    // An initialized transport without a running host is either stopping or
    // left behind by a failed deinit.  It must reach a fully deinitialized
    // state before a new host task can be started.
    if (initialized && !running) {
        return CodexBleLifecycleAction::kStop;
    }
    if (!desired && (initialized || running)) {
        return CodexBleLifecycleAction::kStop;
    }
    if (desired && !running) {
        return CodexBleLifecycleAction::kStart;
    }
    return CodexBleLifecycleAction::kWait;
}

constexpr uint32_t codex_ble_transport_retry_delay_ms(uint32_t attempt)
{
    return attempt == 0 ? 1000U
         : attempt == 1 ? 5000U
         : attempt == 2 ? 15000U
                        : 30000U;
}
