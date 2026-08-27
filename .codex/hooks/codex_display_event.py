"""Forward bounded lifecycle metadata and local token totals to the companion."""
import json
import msvcrt
import os
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local")) / "CodexUsageDisplay"
EVENTS = ROOT / "hook-events.jsonl"
ARCHIVE = ROOT / "hook-events.jsonl.1"
USAGE = ROOT / "local-usage.json"
LOCK = ROOT / "hook-events.lock"
SESSIONS = Path.home() / ".codex" / "sessions"


def locked_descriptor() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    fd = os.open(LOCK, os.O_CREAT | os.O_RDWR)
    if os.fstat(fd).st_size == 0:
        os.write(fd, b"\0")
    os.lseek(fd, 0, os.SEEK_SET)
    msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
    return fd


def token_delta(path: str, offset: int, today: str) -> tuple[int, int]:
    try:
        size = os.path.getsize(path)
        offset = offset if offset <= size else 0
        with open(path, "rb") as handle:
            handle.seek(offset)
            data = handle.read()
    except OSError:
        return 0, offset
    end = data.rfind(b"\n")
    if end < 0:
        return 0, offset
    tokens = 0
    for line in data[:end + 1].splitlines():
        try:
            record = json.loads(line)
            payload = record.get("payload") or {}
            if (record.get("type") == "event_msg" and
                    payload.get("type") == "token_count" and
                    str(record.get("timestamp", "")).startswith(today)):
                usage = (payload.get("info") or {}).get("last_token_usage") or {}
                tokens += max(0, int(usage.get("total_tokens", 0)))
        except (ValueError, TypeError, AttributeError):
            pass
    return tokens, offset + end + 1


def load_usage(today: str) -> dict:
    try:
        value = json.loads(USAGE.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        value = {}
    same_day = value.get("utc_date") == today
    return {"utc_date": today,
            "tokens": max(0, int(value.get("tokens", 0))) if same_day else 0,
            "offsets": value.get("offsets", {})
            if same_day and isinstance(value.get("offsets"), dict) else {}}


def save_usage(value: dict) -> None:
    fd, temporary = tempfile.mkstemp(prefix="local-usage.", suffix=".json", dir=ROOT)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, separators=(",", ":"))
        os.replace(temporary, USAGE)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def update_usage(transcript: object) -> None:
    if not isinstance(transcript, str) or not transcript:
        return
    today = datetime.now(timezone.utc).date().isoformat()
    state = load_usage(today)
    path = os.path.realpath(transcript)
    delta, offset = token_delta(path, max(0, int(state["offsets"].get(path, 0))), today)
    state["tokens"] += delta
    state["offsets"][path] = offset
    save_usage(state)


def append_event(event: dict) -> None:
    if EVENTS.exists() and EVENTS.stat().st_size >= 1024 * 1024:
        try:
            os.replace(EVENTS, ARCHIVE)
        except OSError:
            # Windows may deny rename while the companion has the queue open.
            # Keep a diagnostic copy and truncate the live queue under the
            # inter-process lock so its size remains bounded.
            try:
                data = EVENTS.read_bytes()
                ARCHIVE.write_bytes(data[-1024 * 1024:])
                EVENTS.write_bytes(b"")
            except OSError:
                return
    with EVENTS.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(event, separators=(",", ":")) + "\n")


def bootstrap() -> None:
    today = datetime.now(timezone.utc).date().isoformat()
    state = {"utc_date": today, "tokens": 0, "offsets": {}}
    for path in SESSIONS.rglob("rollout-*.jsonl") if SESSIONS.is_dir() else []:
        resolved = os.path.realpath(path)
        delta, offset = token_delta(resolved, 0, today)
        state["tokens"] += delta
        state["offsets"][resolved] = offset
    save_usage(state)
    print(json.dumps({"utc_date": today, "tokens": state["tokens"]}))


def main() -> int:
    lock = locked_descriptor()
    try:
        if sys.argv[1:] == ["--bootstrap"]:
            bootstrap()
            return 0
        value = json.load(sys.stdin)
        name = value.get("hook_event_name")
        if name not in {"UserPromptSubmit", "Stop"}:
            return 0
        event = {"event": name, "session_id": value.get("session_id"),
                 "turn_id": value.get("turn_id"),
                 "transcript_path": value.get("transcript_path"), "at": time.time()}
        if not event["session_id"] or not event["turn_id"]:
            return 0
        if name == "Stop":
            update_usage(event["transcript_path"])
        append_event(event)
    except (OSError, ValueError, TypeError):
        pass
    finally:
        os.close(lock)
    print("{}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
