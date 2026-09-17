#include "codex_ble_lifecycle_policy.h"

#include <assert.h>

int main()
{
    using Action = CodexBleLifecycleAction;
    using Tick = uint32_t;
    constexpr Tick kNow = 1000;
    constexpr Tick kGracefulStopTicks = 60000U;
    static_assert(kCodexBleGracefulStopMs == 60000U,
                  "graceful stop window must remain 60 seconds");
    constexpr Tick kDeadline = kNow + kGracefulStopTicks;

    // A page entry starts an uninitialized transport; an already running
    // transport remains active while the page is visible.
    assert(codex_ble_lifecycle_action(false, false, false) == Action::kWait);
    assert(codex_ble_lifecycle_action(false, false, true) == Action::kStart);
    assert(codex_ble_lifecycle_action(true, true, true) == Action::kWait);

    // Leaving CODEX holds the initialized/running transport until the grace
    // deadline, including the exact deadline boundary.
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kNow,
                                      kDeadline) == Action::kWait);
    assert(codex_ble_lifecycle_wait_ticks(false,
                                          true,
                                          kNow,
                                          kDeadline) == kGracefulStopTicks);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kDeadline - 1,
                                      kDeadline) == Action::kWait);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kDeadline,
                                      kDeadline) == Action::kStop);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kDeadline + 1,
                                      kDeadline) == Action::kStop);

    // Re-entry cancels a pending graceful stop.  If deinit has already begun,
    // the initialized-but-not-running state must finish stopping first.
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      true,
                                      false,
                                      kNow,
                                      kDeadline) == Action::kWait);
    assert(codex_ble_lifecycle_action(true,
                                      false,
                                      true,
                                      false,
                                      kNow,
                                      kDeadline) == Action::kStop);
    assert(codex_ble_lifecycle_action(false,
                                      false,
                                      true,
                                      false,
                                      kNow,
                                      kDeadline) == Action::kStart);

    // An immediate stop overrides graceful state, including the common
    // false,false -> false,true request upgrade.
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      false,
                                      kNow,
                                      kDeadline) == Action::kStop);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kNow,
                                      kDeadline) == Action::kWait);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      false,
                                      kNow,
                                      kDeadline) == Action::kStop);

    // Tick arithmetic remains correct across uint32_t wraparound.
    constexpr Tick kWrapNow = UINT32_MAX - 5U;
    constexpr Tick kWrapDeadline = kWrapNow + kGracefulStopTicks;
    assert(kWrapDeadline < kWrapNow);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kWrapNow,
                                      kWrapDeadline) == Action::kWait);
    assert(codex_ble_lifecycle_wait_ticks(false,
                                          true,
                                          kWrapNow,
                                          kWrapDeadline) ==
           kGracefulStopTicks);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      kWrapDeadline,
                                      kWrapDeadline) == Action::kStop);
    assert(codex_ble_lifecycle_action(true,
                                      true,
                                      false,
                                      true,
                                      static_cast<Tick>(kWrapDeadline + 1U),
                                      kWrapDeadline) == Action::kStop);

    // The legacy three-argument policy retains immediate-stop semantics.
    assert(codex_ble_lifecycle_action(true, true, false) == Action::kStop);
    assert(codex_ble_lifecycle_action(true, false, false) == Action::kStop);

    assert(codex_ble_transport_retry_delay_ms(0) == 1000);
    assert(codex_ble_transport_retry_delay_ms(1) == 5000);
    assert(codex_ble_transport_retry_delay_ms(2) == 15000);
    assert(codex_ble_transport_retry_delay_ms(3) == 30000);
    assert(codex_ble_transport_retry_delay_ms(100) == 30000);
    return 0;
}
