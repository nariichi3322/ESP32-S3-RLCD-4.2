"""Build a v3 Snapshot for Claude Code from statusline, transcript and hook state."""
import asyncio
import json
import os
import time
from dataclasses import asdict, replace
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

from .metrics import Snapshot
from .paths import CLAUDE_RATE_LIMITS_PATH, CLAUDE_SESSIONS_PATH, claude_directory
from .protocol import encode_snapshot

SESSION_MAX_AGE_SECONDS = 6 * 3600
SCAN_DAYS = 8


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def _window(value: object, now: int) -> tuple[bool, int, int]:
    """Return (available, remaining_percent, reset_seconds) for one window."""
    try:
        used, resets_at = float(value["used_percentage"]), int(value["resets_at"])
        remaining = max(0, min(100, int(100 - used)))  # NaN/inf raise here
    except (KeyError, TypeError, ValueError, OverflowError):
        return False, 0, 0
    if now >= resets_at:  # window rolled over since the statusline last ran
        return True, 100, 0
    return True, remaining, min(resets_at - now, 0xFFFFFFFF)


def _utc_offset_minutes(now: int) -> int:
    return int(datetime.fromtimestamp(now).astimezone().utcoffset().total_seconds() // 60)


def _parse_time(text: object) -> Optional[float]:
    try:
        return datetime.fromisoformat(str(text).replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


def _scan_file(path: str) -> dict:
    """{(message id, request id): (epoch, tokens)}; malformed lines are skipped."""
    found = {}
    with open(path, "rb") as handle:
        for line in handle:
            if b'"assistant"' not in line:
                continue
            try:
                record = json.loads(line)
                message = record["message"]
                usage = message["usage"]
                stamp = _parse_time(record["timestamp"])
                tokens = sum(max(0, int(usage.get(key) or 0)) for key in (
                    "input_tokens", "output_tokens",
                    "cache_creation_input_tokens", "cache_read_input_tokens"))
                if record.get("type") == "assistant" and stamp is not None and message["id"]:
                    found[(message["id"], record.get("requestId"))] = (stamp, tokens)
            except (ValueError, KeyError, TypeError, AttributeError):
                pass
    return found


class TokenScanner:
    def __init__(self, projects_dir: Path) -> None:
        self.projects_dir = Path(projects_dir)
        self.files: dict[str, tuple[int, int, dict]] = {}

    def totals(self, now: float) -> tuple[int, int]:
        today = datetime.fromtimestamp(now).date()
        week_start = today - timedelta(days=6)
        floor = now - SCAN_DAYS * 86400
        seen, fresh = {}, {}
        for root, _, names in os.walk(self.projects_dir):
            for name in names:
                if not name.endswith(".jsonl"):
                    continue
                path = os.path.join(root, name)
                try:
                    stat = os.stat(path)
                    if stat.st_mtime < floor:
                        continue
                    cached = self.files.get(path)
                    if cached and cached[:2] == (stat.st_size, stat.st_mtime_ns):
                        fresh[path] = cached
                    else:
                        fresh[path] = (stat.st_size, stat.st_mtime_ns, _scan_file(path))
                except OSError:
                    continue
                seen.update(fresh[path][2])
        self.files = fresh
        day = week = 0
        for stamp, tokens in seen.values():
            date = datetime.fromtimestamp(stamp).date()
            if date == today:
                day += tokens
            if week_start <= date <= today:
                week += tokens
        return day, week


def running_sessions(path: Path, now: float) -> int:
    sessions = _read_json(path).get("sessions")
    if not isinstance(sessions, dict):
        return 0
    # ponytail: a session whose Stop hook never fired counts as running for 6h; no liveness probe
    return min(255, sum(1 for at in sessions.values()
                        if isinstance(at, (int, float)) and 0 <= now - at < SESSION_MAX_AGE_SECONDS))


def build_claude_snapshot(tokens: tuple[int, int], rate_path: Path = CLAUDE_RATE_LIMITS_PATH,
                          sessions_path: Path = CLAUDE_SESSIONS_PATH,
                          now: Optional[float] = None) -> Snapshot:
    now = time.time() if now is None else now
    seconds = int(now)
    limits = _read_json(rate_path)
    primary_ok, remaining, reset = _window(limits.get("five_hour"), seconds)
    secondary_ok, secondary_remaining, secondary_reset = _window(limits.get("seven_day"), seconds)
    return Snapshot(
        seconds, _utc_offset_minutes(seconds), remaining, 300 if primary_ok else 0, reset,
        secondary_ok, secondary_remaining, 10080 if secondary_ok else 0, secondary_reset,
        tokens[0], False, tokens[1], False, False, 0, 0, 0,
        running_sessions(sessions_path, now))


class ClaudeSnapshotCache:
    def __init__(self, refresh_seconds: float = 60, claude_dir: Optional[Path] = None,
                 rate_path: Path = CLAUDE_RATE_LIMITS_PATH,
                 sessions_path: Path = CLAUDE_SESSIONS_PATH) -> None:
        self.refresh_seconds = refresh_seconds
        self.scanner = TokenScanner((claude_dir or claude_directory()) / "projects")
        self.rate_path, self.sessions_path = rate_path, sessions_path
        self.tokens = (0, 0)
        self.scanned_at: Optional[float] = None
        self.sequence = 0
        self.signature: Optional[dict] = None

    def snapshot(self) -> Snapshot:
        now = time.time()
        refreshed = self.scanned_at is None or now - self.scanned_at >= self.refresh_seconds
        if refreshed:
            self.tokens, self.scanned_at = self.scanner.totals(now), now
        snapshot = build_claude_snapshot(self.tokens, self.rate_path, self.sessions_path, now)
        # The countdown and clock tick every call; only real content changes bump the sequence.
        signature = asdict(replace(snapshot, generated_at=0, quota_reset_seconds=0,
                                   secondary_quota_reset_seconds=0))
        if refreshed or signature != self.signature:
            self.sequence += 1
            self.signature = signature
        return snapshot

    async def encoded(self) -> bytes:
        snapshot = await asyncio.to_thread(self.snapshot)
        return encode_snapshot(snapshot, self.sequence)
