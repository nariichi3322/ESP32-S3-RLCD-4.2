# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import argparse
import asyncio
import ctypes
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


_INSTANCE_MUTEX_NAME = "Local\\CodexUsageDisplayCompanion"
_ERROR_ALREADY_EXISTS = 183


def acquire_instance_mutex() -> tuple[object, int] | None:
    """Return the Win32 mutex owner, or None when a companion already runs."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.argtypes = (ctypes.c_void_p, ctypes.c_bool,
                                      ctypes.c_wchar_p)
    kernel32.CreateMutexW.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)
    kernel32.CloseHandle.restype = ctypes.c_bool
    handle = kernel32.CreateMutexW(None, False, _INSTANCE_MUTEX_NAME)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() == _ERROR_ALREADY_EXISTS:
        kernel32.CloseHandle(handle)
        return None
    return kernel32, handle


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
        self.quota_signature: Optional[tuple[int, int]] = None

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
            signature = (self.snapshot.limit_window_minutes,
                         self.snapshot.secondary_limit_window_minutes
                         if self.snapshot.secondary_available else 0)
            if signature != self.quota_signature:
                logging.info("Codex quota windows: primary=%dm secondary=%s",
                             signature[0],
                             f"{signature[1]}m" if signature[1]
                             else "unavailable")
                self.quota_signature = signature
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
        # Preload the first snapshot before scanning. Once BLE connects, the
        # initial authenticated write must not wait on an app-server round trip.
        await cache.refresh()
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
    mutex = None if args.once else acquire_instance_mutex()
    if not args.once and mutex is None:
        logging.warning("Another Codex Display companion is already running; exiting")
        return
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except Exception:
        logging.exception("Companion exited unexpectedly")
        raise SystemExit(1)
    finally:
        if mutex is not None:
            kernel32, handle = mutex
            kernel32.CloseHandle(handle)
