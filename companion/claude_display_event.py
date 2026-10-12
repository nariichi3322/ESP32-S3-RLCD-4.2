"""Claude Code statusLine/hook helper: stores rate limits and running sessions locally."""
import json
import msvcrt
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local")) / "CodexUsageDisplay"
RATE_LIMITS = ROOT / "claude-rate-limits.json"
SESSIONS = ROOT / "claude-sessions.json"
LOCK = ROOT / "claude-sessions.lock"
MAX_AGE = 6 * 3600


def write_atomic(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.stem + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, separators=(",", ":"))
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def statusline(raw: bytes, chain: str | None) -> None:
    try:
        limits = json.loads(raw).get("rate_limits")
        if isinstance(limits, dict):
            keep = {key: limits[key] for key in ("five_hour", "seven_day")
                    if isinstance(limits.get(key), dict)}
            if keep:
                write_atomic(RATE_LIMITS, {"captured_at": int(time.time()), **keep})
    except (OSError, ValueError, TypeError, AttributeError):
        pass
    if chain:
        try:
            result = subprocess.run(chain, input=raw, capture_output=True, shell=True, timeout=5)
            sys.stdout.buffer.write(result.stdout)
        except (OSError, subprocess.SubprocessError):
            pass


def hook(raw: bytes) -> None:
    value = json.loads(raw)
    name, session = value.get("hook_event_name"), value.get("session_id")
    if name not in {"UserPromptSubmit", "Stop", "SessionEnd"} or not isinstance(session, str) or not session:
        return
    ROOT.mkdir(parents=True, exist_ok=True)
    fd = os.open(LOCK, os.O_CREAT | os.O_RDWR)
    try:
        if os.fstat(fd).st_size == 0:
            os.write(fd, b"\0")
        os.lseek(fd, 0, os.SEEK_SET)
        msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
        try:
            current = json.loads(SESSIONS.read_text(encoding="utf-8")).get("sessions")
        except (OSError, ValueError, AttributeError):
            current = None
        now = time.time()
        sessions = {key: at for key, at in (current if isinstance(current, dict) else {}).items()
                    if isinstance(at, (int, float)) and now - at < MAX_AGE}
        if name == "UserPromptSubmit":
            sessions[session] = now
        else:
            sessions.pop(session, None)
        write_atomic(SESSIONS, {"sessions": sessions})
    finally:
        os.close(fd)


def main(argv: list[str]) -> int:
    try:
        raw = sys.stdin.buffer.read()
        if argv[:1] == ["--statusline"]:
            chain = argv[argv.index("--chain") + 1] if "--chain" in argv[:-1] else None
            if "--chain-file" in argv[:-1]:
                # A file avoids shell-quoting the previous command inside settings.json.
                try:
                    chain = json.loads(Path(argv[argv.index("--chain-file") + 1])
                                       .read_text(encoding="utf-8"))["statusLine"]["command"]
                except (OSError, ValueError, KeyError, TypeError):
                    chain = None
            statusline(raw, chain)
        elif argv[:1] == ["--hook"]:
            hook(raw)
    except Exception:
        pass  # never break Claude Code's UI or turn flow
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
