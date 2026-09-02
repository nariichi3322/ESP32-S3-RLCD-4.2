#include "codex_ble_lifecycle_policy.h"

#include <assert.h>

int main()
{
    using Action = CodexBleLifecycleAction;
    assert(codex_ble_lifecycle_action(false, false, false) == Action::kWait);
    assert(codex_ble_lifecycle_action(false, false, true) == Action::kStart);
    assert(codex_ble_lifecycle_action(true, true, true) == Action::kWait);
    assert(codex_ble_lifecycle_action(true, true, false) == Action::kStop);
    assert(codex_ble_lifecycle_action(true, false, false) == Action::kStop);

    // Re-entering while shutdown is in progress must finish deinit first,
    // then start from a clean transport on the following transition.
    assert(codex_ble_lifecycle_action(true, false, true) == Action::kStop);
    assert(codex_ble_lifecycle_action(false, false, true) == Action::kStart);

    assert(codex_ble_transport_retry_delay_ms(0) == 1000);
    assert(codex_ble_transport_retry_delay_ms(1) == 5000);
    assert(codex_ble_transport_retry_delay_ms(2) == 15000);
    assert(codex_ble_transport_retry_delay_ms(3) == 30000);
    assert(codex_ble_transport_retry_delay_ms(100) == 30000);
    return 0;
}
