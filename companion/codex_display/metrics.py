# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import asyncio
import json
import os
import time
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation, ROUND_DOWN
from typing import Any, Iterable, Optional


@dataclass(frozen=True)
class Snapshot:
    generated_at: int
    utc_offset_minutes: int
    remaining_percent: int
    limit_window_minutes: int
    quota_reset_seconds: int
    secondary_available: bool
    secondary_remaining_percent: int
    secondary_limit_window_minutes: int
    secondary_quota_reset_seconds: int
    tokens_today: int
    tokens_today_estimated: bool
    tokens_7d: int
    paid_credits_available: bool
    paid_credits_unlimited: bool
    paid_credits_balance: int
    reset_credits: int
    next_credit_expiry_seconds: int
    active_threads: int


def _until(timestamp: Optional[int], now: int) -> int:
    return max(0, int(timestamp) - now) if timestamp is not None else 0


def _paid_credits(value: Any) -> tuple[bool, bool, int]:
    if not isinstance(value, dict):
        return False, False, 0
    if value.get("unlimited") is True:
        return True, True, 0
    balance = value.get("balance")
    if balance is None:
        return (True, False, 0) if value.get("hasCredits") is False else (False, False, 0)
    if isinstance(balance, bool):
        return False, False, 0
    try:
        parsed = Decimal(str(balance))
    except (InvalidOperation, ValueError):
        return False, False, 0
    if not parsed.is_finite() or parsed < 0 or parsed > Decimal(0xFFFFFFFF):
        return False, False, 0
    integer = int(parsed.to_integral_value(rounding=ROUND_DOWN))
    return True, False, integer


def thread_is_active(thread: dict[str, Any]) -> bool:
    path = thread.get("path")
    if path and os.path.isfile(path):
        return rollout_is_active(path)
    return (thread.get("status") or {}).get("type") == "active"


def rollout_is_active(path: str, tail_bytes: int = 1024 * 1024,
                      stale_seconds: int = 30 * 60) -> bool:
    try:
        if time.time() - os.path.getmtime(path) > stale_seconds:
            return False
        with open(path, "rb") as handle:
            size = os.path.getsize(path)
            if size > tail_bytes:
                handle.seek(size - tail_bytes)
                handle.readline()
            lines = handle.readlines()
    except OSError:
        return False
    for line in reversed(lines):
        try:
            record = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if record.get("type") != "event_msg":
            continue
        kind = (record.get("payload") or {}).get("type")
        if kind == "task_complete":
            return False
        if kind == "task_started":
            return True
    return False


def rollout_turn_state(path: str, turn_id: str,
                       tail_bytes: int = 1024 * 1024) -> Optional[bool]:
    try:
        with open(path, "rb") as handle:
            size = os.path.getsize(path)
            if size > tail_bytes:
                handle.seek(size - tail_bytes)
                handle.readline()
            lines = handle.readlines()
    except OSError:
        return None
    for line in reversed(lines):
        try:
            record = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        payload = record.get("payload") or {}
        if record.get("type") != "event_msg" or payload.get("turn_id") != turn_id:
            continue
        if payload.get("type") == "task_complete":
            return False
        if payload.get("type") == "task_started":
            return True
    return None


def active_thread_state(threads: Iterable[dict[str, Any]]) -> tuple[int, set[str]]:
    active = [thread for thread in threads if thread_is_active(thread)]
    return len(active), {os.path.realpath(thread["path"]) for thread in active if thread.get("path")}


def build_snapshot(rate_limits: dict[str, Any], usage: dict[str, Any],
                   threads: Iterable[dict[str, Any]], now: Optional[int] = None,
                   utc_date: Optional[date] = None,
                   utc_offset_minutes: Optional[int] = None) -> Snapshot:
    now = int(time.time()) if now is None else now
    utc_date = datetime.now(timezone.utc).date() if utc_date is None else utc_date
    if utc_offset_minutes is None:
        local = time.localtime(now)
        utc_offset_minutes = int(((-time.altzone if local.tm_isdst > 0 and time.daylight
                                   else -time.timezone)) // 60)
    legacy_limits = rate_limits.get("rateLimits") or {}
    codex_limits = (rate_limits.get("rateLimitsByLimitId") or {}).get("codex") or {}
    primary = codex_limits.get("primary") or legacy_limits.get("primary") or {}
    # Some app-server/account combinations publish primary in
    # rateLimitsByLimitId.codex while leaving the longer window only in the
    # legacy rateLimits object.  Resolve each window independently instead of
    # discarding that valid secondary object when the Codex entry exists.
    secondary_value = codex_limits.get("secondary")
    if not isinstance(secondary_value, dict) or not all(
            secondary_value.get(key) is not None for key in
            ("usedPercent", "windowDurationMins", "resetsAt")):
        secondary_value = legacy_limits.get("secondary")
    secondary = secondary_value if isinstance(secondary_value, dict) else {}
    secondary_available = all(secondary.get(key) is not None for key in
                              ("usedPercent", "windowDurationMins", "resetsAt"))
    used = max(0, min(int(primary.get("usedPercent", 100)), 100))
    secondary_used = max(0, min(int(secondary.get("usedPercent", 100)), 100))
    buckets = {item.get("startDate"): max(0, int(item.get("tokens", 0)))
               for item in usage.get("dailyUsageBuckets") or [] if item.get("startDate")}
    today = utc_date.isoformat()
    credits = rate_limits.get("rateLimitResetCredits") or {}
    paid_value = codex_limits.get("credits")
    if not isinstance(paid_value, dict):
        paid_value = legacy_limits.get("credits")
    paid_available, paid_unlimited, paid_balance = _paid_credits(paid_value)
    expiries = [int(item["expiresAt"]) for item in credits.get("credits") or []
                if item.get("status") == "available" and item.get("expiresAt") is not None
                and int(item["expiresAt"]) > now]
    return Snapshot(
        now, utc_offset_minutes, 100 - used,
        max(0, int(primary.get("windowDurationMins") or 0)),
        _until(primary.get("resetsAt"), now), secondary_available,
        100 - secondary_used if secondary_available else 0,
        max(0, int(secondary.get("windowDurationMins") or 0)) if secondary_available else 0,
        _until(secondary.get("resetsAt"), now) if secondary_available else 0,
        buckets.get(today, 0),
        today not in buckets,
        sum(buckets.get((utc_date - timedelta(days=i)).isoformat(), 0) for i in range(7)),
        paid_available,
        paid_unlimited,
        paid_balance,
        max(0, int(credits.get("availableCount", 0))),
        _until(min(expiries) if expiries else None, now),
        active_thread_state(threads)[0])


async def gather_metrics(client: Any) -> list[dict[str, Any]]:
    return list(await asyncio.gather(
        client.request("account/rateLimits/read"),
        client.request("account/usage/read"),
        client.request("thread/list", {"limit": 100, "sortKey": "recency_at",
                                      "sortDirection": "desc", "useStateDbOnly": True})))


async def collect_snapshot_state(client: Any) -> tuple[Snapshot, set[str]]:
    limits, usage, result = await gather_metrics(client)
    threads = result.get("data") or []
    return build_snapshot(limits, usage, threads), active_thread_state(threads)[1]


async def collect_active_thread_state(client: Any) -> tuple[int, set[str]]:
    result = await client.request("thread/list", {"limit": 100,
        "sortKey": "recency_at", "sortDirection": "desc", "useStateDbOnly": True})
    return active_thread_state(result.get("data") or [])
