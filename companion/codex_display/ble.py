import asyncio
import logging
import platform
from collections.abc import Awaitable, Callable
from typing import Any, Optional

from .constants import DEVICE_NAME, SERVICE_UUID, STATUS_UUID

logger = logging.getLogger(__name__)

PAIRING_WAIT_SECONDS = 75.0
PAIRING_RETRY_SECONDS = 1.0
FAST_GATT_RETRY_SECONDS = 1.0
FAST_GATT_RETRY_ATTEMPTS = 3
POST_LINK_REFRESH_TIMEOUT_SECONDS = 10.0


def _characteristic_missing(error: Exception) -> bool:
    message = str(error).lower()
    return "characteristic" in message and "not found" in message


def _client_options(disconnected_callback: Callable[[Any], None]) -> dict:
    # The ESP32 initiates display-only SMP after connecting. Do not ask
    # Bleak/WinRT to pair first: Bleak 1.x uses a confirm-only ceremony and can
    # fall back to an encrypted-but-unauthenticated bond on Windows.
    return {
        "timeout": 60,
        "disconnected_callback": disconnected_callback,
        # Firmware updates can change characteristic handles while Windows
        # retains the bonded device's old GATT database. Always rediscover the
        # live attribute table instead of accepting that stale cache.
        "winrt": {"use_cached_services": False},
    }


def _enable_winrt_service_changed_retry(client: Any) -> bool:
    """Make Bleak repeat discovery when Windows reports Service Changed.

    Bleak's WinRT backend already contains the required race-safe discovery
    path, but the installed release leaves it disabled.  Keep this guarded so
    a future Bleak release or a non-WinRT test client remains compatible.
    """
    backend = getattr(client, "_backend", None)
    if backend is None or not hasattr(backend, "_retry_on_services_changed"):
        return False
    backend._retry_on_services_changed = True
    return True


class BleCompanion:
    """Windows-only, one-way status client; no command characteristic exists."""

    def __init__(self, snapshot_provider: Callable[[], Awaitable[bytes]],
                 request_refresh: Optional[Callable[[], Awaitable[None]]] = None,
                 device_name: str = DEVICE_NAME, heartbeat_seconds: float = 15.0,
                 status_changed: Optional[asyncio.Event] = None,
                 initial_snapshot_provider: Optional[
                     Callable[[], Awaitable[bytes]]] = None) -> None:
        self.snapshot_provider = snapshot_provider
        self.initial_snapshot_provider = (initial_snapshot_provider or
                                          snapshot_provider)
        self.request_refresh = request_refresh
        self.device_name = device_name
        self.heartbeat_seconds = heartbeat_seconds
        self.status_changed = status_changed
        self.connected_event = asyncio.Event()

    async def find_device(self):
        from bleak import BleakScanner
        return await BleakScanner.find_device_by_filter(
            lambda device, advertisement: (
                (device.name or "") == self.device_name or
                SERVICE_UUID.lower() in [uuid.lower() for uuid in advertisement.service_uuids]
            ), timeout=10.0, service_uuids=[SERVICE_UUID])

    async def run_connection(self, device) -> None:
        from bleak import BleakClient
        disconnected = asyncio.Event()

        def on_disconnect(_: Any) -> None:
            self.connected_event.clear()
            disconnected.set()

        client = BleakClient(device, **_client_options(on_disconnect))
        _enable_winrt_service_changed_retry(client)
        try:
            await client.connect()
            await self._send_after_authentication(client)
            self.connected_event.set()
            logger.info("Codex Display LINKED")
            if self.request_refresh is not None:
                try:
                    await asyncio.wait_for(self.request_refresh(),
                                           POST_LINK_REFRESH_TIMEOUT_SECONDS)
                    await self._send(client)
                except asyncio.TimeoutError:
                    logger.warning("Post-link Codex refresh timed out; keeping BLE connected")
                except Exception:
                    logger.exception("Post-link Codex refresh failed; keeping cached BLE data")
            while client.is_connected and not disconnected.is_set():
                if self.status_changed is None:
                    try:
                        await asyncio.wait_for(disconnected.wait(), self.heartbeat_seconds)
                        break
                    except asyncio.TimeoutError:
                        pass
                else:
                    changed = asyncio.create_task(self.status_changed.wait())
                    lost = asyncio.create_task(disconnected.wait())
                    done, pending = await asyncio.wait(
                        [changed, lost], timeout=self.heartbeat_seconds,
                        return_when=asyncio.FIRST_COMPLETED)
                    for task in pending:
                        task.cancel()
                    await asyncio.gather(*pending, return_exceptions=True)
                    if lost in done:
                        break
                    self.status_changed.clear()
                await self._send(client)
        finally:
            self.connected_event.clear()
            if client.is_connected:
                try:
                    await asyncio.wait_for(client.disconnect(), 5)
                except Exception as error:
                    logger.warning("BLE disconnect failed: %s", error)

    async def _send(self, client: Any,
                    provider: Optional[Callable[[], Awaitable[bytes]]] = None) -> None:
        snapshot_provider = provider or self.snapshot_provider
        await asyncio.wait_for(client.write_gatt_char(
            STATUS_UUID, await snapshot_provider(), response=True), 10)

    async def _send_after_authentication(self, client: Any) -> None:
        """Keep the BLE link alive while Windows completes passkey pairing."""
        deadline = asyncio.get_running_loop().time() + PAIRING_WAIT_SECONDS
        waiting_logged = False
        while True:
            try:
                await self._send(client, self.initial_snapshot_provider)
                return
            except Exception as error:
                message = str(error).lower()
                authentication_pending = (
                    "insufficient authentication" in message or
                    "protocol error 0x05" in message)
                remaining = deadline - asyncio.get_running_loop().time()
                if (not authentication_pending or not client.is_connected or
                        remaining <= 0):
                    raise
                if not waiting_logged:
                    logger.info(
                        "Waiting for Bluetooth passkey authentication...")
                    waiting_logged = True
                await asyncio.sleep(min(PAIRING_RETRY_SECONDS, remaining))

    async def run_forever(self) -> None:
        if platform.system() != "Windows":
            raise RuntimeError("Codex Usage companion supports Windows only")
        backoff = 1.0
        while True:
            device_found = False
            try:
                logger.info("Scanning for %s...", self.device_name)
                device = await self.find_device()
                if device is None:
                    raise RuntimeError("Codex Display not found")
                device_found = True
                backoff = 1.0
                for attempt in range(FAST_GATT_RETRY_ATTEMPTS):
                    try:
                        await self.run_connection(device)
                        break
                    except Exception as error:
                        if (not _characteristic_missing(error) or
                                attempt + 1 >= FAST_GATT_RETRY_ATTEMPTS):
                            raise
                        logger.info("GATT status characteristic unavailable; fast retry %d/%d",
                                    attempt + 2, FAST_GATT_RETRY_ATTEMPTS)
                        await asyncio.sleep(FAST_GATT_RETRY_SECONDS)
            except asyncio.CancelledError:
                raise
            except Exception as error:
                self.connected_event.clear()
                logger.warning("BLE connection interrupted: %s", error)
            await asyncio.sleep(backoff)
            if not device_found:
                backoff = min(backoff * 2.0, 60.0)
