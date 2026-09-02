// Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
#pragma once

#include <stdint.h>

struct CodexPairingSnapshot {
    uint32_t passkey = 0;
    uint32_t expires_tick_ms = 0;
    uint32_t generation = 0;
    bool visible = false;
};

bool codex_usage_ble_request_enabled(bool enabled);
bool codex_usage_ble_clear_bonds();
bool codex_usage_ble_pairing_snapshot(CodexPairingSnapshot *out);
void codex_usage_ble_clear_pairing_overlay();

