# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import argparse
import asyncio
import logging
import os
import platform
from dataclasses import replace
from logging.handlers import RotatingFileHandler
from typing import Optional

from .app_server import AppServerClient
from .ble import BleCompanion
from .hook_events import HookActivityTracker
from .local_usage import read_local_tokens
from .metrics import Snapshot, collect_active_thread_state, collect_snapshot_state
from .protocol import encode_snapshot


class SnapshotCache:
    def __init__(self, client: AppServerClient, refresh_seconds: float) -> None:
        self.client, self.refresh_seconds = client, refresh_seconds
        self.snapshot: Optional[Snapshot] = None
        self.updated_at = 0.0
        self.sequence = 0
        self.lock = asyncio.Lock()
        self.active_paths: set[str] = set()
        self.activity: Optional[HookActivityTracker] = None
        self.status_changed = asyncio.Event()

    def attach_activity(self, activity: HookActivityTracker) -> None:
        self.activity = activity

    def current(self) -> Snapshot:
        assert self.snapshot is not None
        snapshot = self.snapshot
        local = read_local_tokens()
        if (snapshot.tokens_today_estimated and local is not None and
                local > snapshot.tokens_today):
            snapshot = replace(snapshot, tokens_today=local, tokens_today_estimated=True)
        extra = self.activity.extra_count(self.active_paths) if self.activity else 0
        return replace(snapshot, active_threads=snapshot.active_threads + extra)

    async def encoded(self) -> bytes:
        if self.snapshot is None or asyncio.get_running_loop().time() - self.updated_at >= self.refresh_seconds:
            try:
                await self.refresh()
            except Exception:
                if self.snapshot is None:
                    raise
                logging.exception("Snapshot refresh failed; sending cached data")
        return encode_snapshot(self.current(), self.sequence)

    async def refresh(self) -> None:
        async with self.lock:
            self.snapshot, self.active_paths = await collect_snapshot_state(self.client)
            self.updated_at = asyncio.get_running_loop().time()
            self.sequence += 1

    async def activity_changed(self) -> None:
        try:
            count, paths = await collect_active_thread_state(self.client)
        except Exception:
            count, paths = None, set()
        async with self.lock:
            if self.snapshot is not None and count is not None:
                self.snapshot = replace(self.snapshot, active_threads=count)
                self.active_paths = paths
            self.sequence += 1
            self.status_changed.set()

    async def reconcile(self) -> None:
        count, paths = await collect_active_thread_state(self.client)
        async with self.lock:
            if self.snapshot is not None:
                before = self.current().active_threads
                self.snapshot = replace(self.snapshot, active_threads=count)
                self.active_paths = paths
                if self.current().active_threads != before:
                    self.sequence += 1
                    self.status_changed.set()


async def reconcile_loop(cache: SnapshotCache, seconds: float,
                         connected: asyncio.Event) -> None:
    while True:
        await asyncio.sleep(seconds)
        if connected.is_set():
            try:
                await cache.reconcile()
            except Exception:
                logging.exception("Periodic RUN reconciliation failed")


async def run(args: argparse.Namespace) -> None:
    if platform.system() != "Windows":
        raise RuntimeError("This companion supports Windows only")
    client = AppServerClient(args.codex_bin)
    await client.start()
    try:
        cache = SnapshotCache(client, args.refresh_seconds)
        if args.once:
            print((await cache.encoded()).decode())
            return
        activity = HookActivityTracker(cache.activity_changed)
        cache.attach_activity(activity)
        ble = BleCompanion(cache.encoded, cache.refresh, args.device,
                           args.heartbeat_seconds, cache.status_changed)
        tasks = [asyncio.create_task(activity.run_forever()),
                 asyncio.create_task(reconcile_loop(cache, args.run_reconcile_seconds,
                                                    ble.connected_event)),
                 asyncio.create_task(ble.run_forever()),
                 asyncio.create_task(client.wait_for_exit())]
        try:
            done, _ = await asyncio.wait(tasks[2:], return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
    finally:
        await client.stop()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Codex Usage Display Windows BLE companion")
    result.add_argument("--device", default="Codex Display")
    result.add_argument("--codex-bin")
    result.add_argument("--refresh-seconds", type=float, default=60)
    result.add_argument("--heartbeat-seconds", type=float, default=15)
    result.add_argument("--run-reconcile-seconds", type=float, default=30)
    result.add_argument("--log-path")
    result.add_argument("--once", action="store_true")
    result.add_argument("--verbose", action="store_true")
    return result


def main() -> None:
    args = parser().parse_args()
    log_path = args.log_path or os.environ.get("CODEX_DISPLAY_LOG")
    handlers = [RotatingFileHandler(log_path, maxBytes=1024 * 1024,
                                    backupCount=1, encoding="utf-8")] if log_path else None
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", handlers=handlers)
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except Exception:
        logging.exception("Companion exited unexpectedly")
        raise SystemExit(1)
