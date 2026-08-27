# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import asyncio
import json
import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable, Optional

from .metrics import rollout_turn_state
from .paths import EVENTS_PATH


@dataclass
class PendingTurn:
    transcript_path: Optional[str]
    phase: str
    created_at: float
    counted: bool


class HookActivityTracker:
    def __init__(self, on_change: Callable[[], Awaitable[None]],
                 events_path: Path = EVENTS_PATH, start_timeout_seconds: float = 10,
                 stale_seconds: float = 30 * 60, poll_seconds: float = 1) -> None:
        self._on_change = on_change
        self._events_path = events_path
        self._start_timeout = start_timeout_seconds
        self._stale = stale_seconds
        self._poll = poll_seconds
        self._turns: dict[tuple[str, str], PendingTurn] = {}

    def extra_count(self, base_active_paths: set[str]) -> int:
        identities = set()
        for key, turn in self._turns.items():
            if not turn.counted:
                continue
            if turn.transcript_path:
                path = os.path.realpath(turn.transcript_path)
                if path not in base_active_paths:
                    identities.add(("path", path))
            else:
                identities.add(("session", key[0]))
        return len(identities)

    async def process_event(self, event: dict) -> None:
        name, session, turn_id = event.get("event"), event.get("session_id"), event.get("turn_id")
        if name not in {"UserPromptSubmit", "Stop"} or not isinstance(session, str) or not isinstance(turn_id, str):
            return
        key = (session, turn_id)
        path = event.get("transcript_path") if isinstance(event.get("transcript_path"), str) else None
        now = float(event.get("at") or time.time())
        if name == "UserPromptSubmit":
            self._turns[key] = PendingTurn(path, "pending_start" if path else "active", now, True)
        else:
            old = self._turns.get(key)
            resolved = path or (old.transcript_path if old else None)
            if resolved:
                self._turns[key] = PendingTurn(resolved, "pending_stop",
                                               old.created_at if old else now,
                                               old.counted if old else False)
            else:
                self._turns.pop(key, None)
        await self._on_change()

    async def reconcile(self, now: Optional[float] = None) -> None:
        now = time.time() if now is None else now
        changed = False
        for key, turn in list(self._turns.items()):
            state = rollout_turn_state(turn.transcript_path, key[1]) if turn.transcript_path else None
            age = now - turn.created_at
            if state is False or (turn.phase == "pending_start" and age >= self._start_timeout) or age >= self._stale:
                del self._turns[key]
                changed = True
            elif state is True and turn.phase == "pending_start":
                turn.phase = "active"
                changed = True
        if changed:
            await self._on_change()

    async def run_forever(self) -> None:
        self._events_path.parent.mkdir(parents=True, exist_ok=True)
        self._events_path.touch(exist_ok=True)
        handle = self._events_path.open("r", encoding="utf-8")
        try:
            handle.seek(0, os.SEEK_END)
            while True:
                position = handle.tell()
                line = handle.readline()
                if line:
                    if not line.endswith("\n"):
                        handle.seek(position)
                    else:
                        try:
                            await self.process_event(json.loads(line))
                        except json.JSONDecodeError:
                            pass
                else:
                    try:
                        if self._events_path.stat().st_size < handle.tell():
                            handle.close()
                            handle = self._events_path.open("r", encoding="utf-8")
                    except OSError:
                        pass
                    await self.reconcile()
                    await asyncio.sleep(self._poll)
        finally:
            handle.close()
