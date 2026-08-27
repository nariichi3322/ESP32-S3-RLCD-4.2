# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import json
from dataclasses import asdict

from .constants import PROTOCOL_VERSION
from .metrics import Snapshot


def encode_snapshot(snapshot: Snapshot, sequence: int) -> bytes:
    if isinstance(sequence, bool) or not isinstance(sequence, int) or not 1 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence must be positive")
    values = asdict(snapshot)
    if any(isinstance(value, bool) or not isinstance(value, int)
           for key, value in values.items() if key != "tokens_today_estimated"):
        raise ValueError("snapshot numeric values must be integers")
    if not isinstance(values["tokens_today_estimated"], bool):
        raise ValueError("estimated flag must be boolean")
    if not 0 <= values["remaining_percent"] <= 100:
        raise ValueError("remaining percent out of range")
    if not -840 <= values["utc_offset_minutes"] <= 840:
        raise ValueError("UTC offset out of range")
    numeric = [value for key, value in values.items()
               if key != "utc_offset_minutes" and not isinstance(value, bool)]
    if any(value < 0 for value in numeric):
        raise ValueError("snapshot values must not be negative")
    if values["generated_at"] > 0xFFFFFFFFFFFFFFFF or \
       values["tokens_today"] > 0xFFFFFFFFFFFFFFFF or \
       values["tokens_7d"] > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("64-bit snapshot value out of range")
    if any(values[key] > 0xFFFFFFFF for key in
           ("limit_window_minutes", "quota_reset_seconds",
            "next_credit_expiry_seconds")):
        raise ValueError("32-bit snapshot value out of range")
    if values["active_threads"] > 255 or values["reset_credits"] > 255:
        raise ValueError("8-bit snapshot value out of range")
    payload = {
        "v": PROTOCOL_VERSION, "s": sequence,
        "t": snapshot.generated_at, "o": snapshot.utc_offset_minutes,
        "r": snapshot.remaining_percent, "u": snapshot.limit_window_minutes,
        "q": snapshot.quota_reset_seconds, "d": snapshot.tokens_today,
        "e": int(snapshot.tokens_today_estimated), "w": snapshot.tokens_7d,
        "c": snapshot.reset_credits, "x": snapshot.next_credit_expiry_seconds,
        "a": snapshot.active_threads,
    }
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    if len(encoded) > 180:
        raise ValueError("status payload exceeds the 180-byte budget")
    return encoded
