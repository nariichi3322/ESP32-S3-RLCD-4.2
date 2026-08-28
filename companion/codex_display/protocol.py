# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import json
from dataclasses import asdict

from .constants import PROTOCOL_VERSION
from .metrics import Snapshot


def encode_snapshot(snapshot: Snapshot, sequence: int) -> bytes:
    if isinstance(sequence, bool) or not isinstance(sequence, int) or not 1 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence must be positive")
    values = asdict(snapshot)
    bool_fields = {"tokens_today_estimated", "secondary_available"}
    if any(isinstance(value, bool) or not isinstance(value, int)
           for key, value in values.items() if key not in bool_fields):
        raise ValueError("snapshot numeric values must be integers")
    if any(not isinstance(values[key], bool) for key in bool_fields):
        raise ValueError("snapshot flags must be boolean")
    if not 0 <= values["remaining_percent"] <= 100 or not 0 <= values["secondary_remaining_percent"] <= 100:
        raise ValueError("remaining percent out of range")
    if not -840 <= values["utc_offset_minutes"] <= 840:
        raise ValueError("UTC offset out of range")
    numeric = [value for key, value in values.items()
               if key != "utc_offset_minutes" and key not in bool_fields]
    if any(value < 0 for value in numeric):
        raise ValueError("snapshot values must not be negative")
    if values["generated_at"] > 0xFFFFFFFFFFFFFFFF or \
       values["tokens_today"] > 0xFFFFFFFFFFFFFFFF or \
       values["tokens_7d"] > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("64-bit snapshot value out of range")
    if any(values[key] > 0xFFFFFFFF for key in
           ("limit_window_minutes", "quota_reset_seconds",
            "secondary_limit_window_minutes", "secondary_quota_reset_seconds",
            "next_credit_expiry_seconds")):
        raise ValueError("32-bit snapshot value out of range")
    if values["active_threads"] > 255 or values["reset_credits"] > 255:
        raise ValueError("8-bit snapshot value out of range")
    if not values["secondary_available"] and any(values[key] != 0 for key in
            ("secondary_remaining_percent", "secondary_limit_window_minutes",
             "secondary_quota_reset_seconds")):
        raise ValueError("unavailable secondary window must be empty")
    payload = {
        "v": PROTOCOL_VERSION, "s": sequence,
        "t": snapshot.generated_at, "o": snapshot.utc_offset_minutes,
        "r": snapshot.remaining_percent, "u": snapshot.limit_window_minutes,
        "q": snapshot.quota_reset_seconds, "d": snapshot.tokens_today,
        "n": int(snapshot.secondary_available),
        "R": snapshot.secondary_remaining_percent,
        "U": snapshot.secondary_limit_window_minutes,
        "Q": snapshot.secondary_quota_reset_seconds,
        "e": int(snapshot.tokens_today_estimated), "w": snapshot.tokens_7d,
        "c": snapshot.reset_credits, "x": snapshot.next_credit_expiry_seconds,
        "a": snapshot.active_threads,
    }
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    if len(encoded) > 180:
        raise ValueError("status payload exceeds the 180-byte budget")
    return encoded
