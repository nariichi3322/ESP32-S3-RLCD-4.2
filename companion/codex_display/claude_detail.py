"""Encode the locally cached Claude detail response for the BLE characteristic."""
import json
import re
import struct
import time
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path

from .paths import CLAUDE_DETAIL_PATH

MAX_U64 = (1 << 64) - 1
HEADER = struct.Struct("<BBBBQQHQ3sBQQ")
GRANT = struct.Struct("<BBBQQQ")
SOURCE_STATES = {"ok": 1, "no_credential": 2, "auth_rejected": 3,
                 "rate_limited": 4, "error": 5}
FLAG_NAMES = ("fable_known", "fable_present", "spend_known", "spend_enabled",
              "amount_valid", "grants_known", "more_grants")
WINDOWS = {"five_hour": 1, "seven_day": 2, "seven_day_overage_included": 4}


def _object(value: object) -> dict:
    return value if isinstance(value, dict) else {}


def _u64(value: object) -> int | None:
    return value if type(value) is int and 0 <= value <= MAX_U64 else None


def parse_timestamp(value: object) -> int | None:
    """Parse a timezone-aware ISO timestamp, returning None when invalid."""
    if not isinstance(value, str):
        return None
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if stamp.utcoffset() is None:
            return None
        elapsed = stamp.astimezone(timezone.utc) - datetime(1970, 1, 1, tzinfo=timezone.utc)
        return _u64(elapsed.days * 86400 + elapsed.seconds)
    except (ValueError, OverflowError, OSError):
        return None


def _optional_timestamp(value: object) -> int | None:
    return 0 if value is None else parse_timestamp(value)


def read_claude_detail(path: Path = CLAUDE_DETAIL_PATH) -> dict:
    try:
        return _object(json.loads(Path(path).read_text(encoding="utf-8")))
    except (OSError, ValueError):
        return {}


def _fable(body: dict) -> tuple[int, int, int]:
    limits = body.get("limits")
    if not isinstance(limits, list):
        return 0, 0, 0
    for item in limits:
        if not isinstance(item, dict) or not isinstance(item.get("kind"), str):
            return 0, 0, 0
        if item["kind"] != "weekly_scoped":
            continue
        scope = _object(item.get("scope"))
        model = scope.get("model")
        if model is None and scope.get("surface") is not None:
            continue
        name = _object(model).get("display_name")
        if not isinstance(name, str):
            return 0, 0, 0
        if name.casefold() != "fable":
            continue
        value = item.get("percent")
        try:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                return 0, 0, 0
            percent = Decimal(str(value))
            if not percent.is_finite() or not 0 <= percent <= 100:
                return 0, 0, 0
            used = int((percent * 100).quantize(Decimal(1), rounding=ROUND_HALF_UP))
        except (InvalidOperation, ValueError, OverflowError):
            return 0, 0, 0
        resets = _optional_timestamp(item.get("resets_at"))
        return (3, used, resets) if resets is not None else (0, 0, 0)
    return 1, 0, 0


def _spend(body: dict) -> tuple[int, bytes, int, int, int]:
    spend = body.get("spend")
    if isinstance(spend, dict):
        enabled = spend.get("enabled")
        used, limit = _object(spend.get("used")), _object(spend.get("limit"))
        amount, cap = used.get("amount_minor"), limit.get("amount_minor")
        currency, exponent = used.get("currency"), used.get("exponent")
        matching_limit = (("currency" not in limit or limit["currency"] == currency) and
                          ("exponent" not in limit or limit["exponent"] == exponent))
    else:
        spend = _object(body.get("extra_usage"))
        enabled = spend.get("is_enabled")
        amount, cap = spend.get("used_credits"), spend.get("monthly_limit")
        currency, exponent = spend.get("currency"), spend.get("decimal_places")
        matching_limit = True
    if type(enabled) is not bool:
        return 0, b"\0\0\0", 255, 0, 0
    if not enabled:
        return 4, b"\0\0\0", 255, 0, 0
    flags = 12
    if (not isinstance(currency, str) or re.fullmatch(r"[A-Z]{3}", currency) is None or
            type(exponent) is not int or not 0 <= exponent <= 9 or
            _u64(amount) is None or _u64(cap) is None or not matching_limit):
        return flags, b"\0\0\0", 255, 0, 0
    return flags | 16, currency.encode("ascii"), exponent, amount, cap


def _grants(body: dict, now: int) -> tuple[int, list[tuple[int, int, int, int, int, int]]]:
    items = _object(body.get("cedar_ember")).get("grants")
    if not isinstance(items, list):
        return 0, []
    found = []
    for item in items:
        if not isinstance(item, dict) or type(item.get("paused")) is not bool:
            return 0, []
        if item["paused"]:
            continue
        left = _u64(item.get("resets_left"))
        if left is None:
            return 0, []
        if left == 0:
            continue
        starts, ends = (_optional_timestamp(item.get(key)) for key in ("starts_at", "ends_at"))
        if starts is None or ends is None:
            return 0, []
        if starts > now or (ends and ends < now):
            continue
        total, clears, label, usable = (item.get(key) for key in
                                        ("resets_total", "clears", "label", "usable_now"))
        if (_u64(total) is None or not isinstance(clears, list) or
                not all(isinstance(window, str) for window in clears) or
                not isinstance(label, str) or type(usable) is not bool):
            return 0, []
        mask = 0
        for window in clears:
            mask |= WINDOWS.get(window, 8)
        kind = (1 if mask & 3 == 3 else 2 if "fable" in label.casefold()
                else 3 if mask & 7 else 0)
        found.append((kind, mask, 2 if usable else 0, left, total, ends))
    found.sort(key=lambda item: (not bool(item[2] & 2), item[5] or MAX_U64))
    return 32 | (64 if len(found) > 2 else 0), found[:2]


def encode_claude_detail(detail: dict | None, now: int) -> bytes:
    """Build the fixed 104-byte v1 frame from a detail file and Unix seconds."""
    if _u64(now) is None:
        raise ValueError("invalid frame timestamp")
    detail = _object(detail)
    last_ok, attempt = _object(detail.get("last_ok")), _object(detail.get("last_attempt"))
    captured = _u64(last_ok.get("captured_at")) or 0
    attempt_at = _u64(attempt.get("at"))
    result = attempt.get("result")
    if attempt_at is not None and attempt_at >= captured:
        state = SOURCE_STATES.get(result, 0) if isinstance(result, str) else 0
    else:
        state = 1 if captured else 0
    body = _object(last_ok.get("body")) if captured else {}
    fable_flags, used_bp, resets = _fable(body)
    spend_flags, currency, exponent, used, limit = _spend(body)
    grant_flags, grants = _grants(body, now)
    frame = bytearray(104)
    HEADER.pack_into(frame, 0, 1, state, fable_flags | spend_flags | grant_flags,
                     len(grants), captured, now, used_bp, resets, currency, exponent, used, limit)
    for index, grant in enumerate(grants):
        GRANT.pack_into(frame, HEADER.size + index * GRANT.size, *grant)
    return bytes(frame)


def current_claude_detail() -> bytes:
    return encode_claude_detail(read_claude_detail(), int(time.time()))


def decode_claude_detail(frame: bytes) -> dict:
    if len(frame) != 104:
        raise ValueError("Claude detail frame must be 104 bytes")
    (version, state, flags, count, captured, sent, fable_used, fable_reset,
     currency, exponent, used, limit) = HEADER.unpack_from(frame)
    if version != 1 or flags & 128 or count > 2:
        raise ValueError("invalid Claude detail frame")
    grants = []
    for index in range(count):
        kind, clears, grant_state, left, total, ends = GRANT.unpack_from(
            frame, HEADER.size + index * GRANT.size)
        grants.append({"type": kind, "clears": clears, "state": grant_state,
                       "resets_left": left, "resets_total": total, "ends_at": ends})
    return {"version": version, "source_state": state,
            "source_result": next((name for name, number in SOURCE_STATES.items()
                                   if number == state), "unknown"),
            "flags": flags, "flag_names": [name for bit, name in enumerate(FLAG_NAMES)
                                          if flags & (1 << bit)],
            "captured_at": captured, "sent_at": sent, "fable_used_bp": fable_used,
            "fable_resets_at": fable_reset, "currency": currency.decode("ascii").strip("\0"),
            "exponent": exponent, "spend_used": used, "spend_limit": limit, "grants": grants}
