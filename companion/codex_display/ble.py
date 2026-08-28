import asyncio
import logging
import platform
from collections.abc import Awaitable, Callable
from typing import Any, Optional

from .constants import DEVICE_NAME, SERVICE_UUID, STATUS_UUID

logger = logging.getLogger(__name__)

PAIRING_WAIT_SECONDS = 75.0
PAIRING_RETRY_SECONDS = 1.0


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


class BleCompanion:
    """Windows-only, one-way status client; no command characteristic exists."""

    def __init__(self, snapshot_provider: Callable[[], Awaitable[bytes]],
                 request_refresh: Optional[Callable[[], Awaitable[None]]] = None,
                 device_name: str = DEVICE_NAME, heartbeat_seconds: float = 15.0,
                 status_changed: Optional[asyncio.Event] = None) -> None:
        self.snapshot_provider = snapshot_provider
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
        try:
            await client.connect()
            if self.request_refresh is not None:
                await self.request_refresh()
            await self._send_after_authentication(client)
            self.connected_event.set()
            logger.info("Codex Display LINKED")
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

    async def _send(self, client: Any) -> None:
        await asyncio.wait_for(client.write_gatt_char(
            STATUS_UUID, await self.snapshot_provider(), response=True), 10)

    async def _send_after_authentication(self, client: Any) -> None:
        """Keep the BLE link alive while Windows completes passkey pairing."""
        deadline = asyncio.get_running_loop().time() + PAIRING_WAIT_SECONDS
        waiting_logged = False
        while True:
            try:
                await self._send(client)
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
            connected_at = None
            try:
                logger.info("Scanning for %s...", self.device_name)
                device = await self.find_device()
                if device is None:
                    raise RuntimeError("Codex Display not found")
                connected_at = asyncio.get_running_loop().time()
                await self.run_connection(device)
            except asyncio.CancelledError:
                raise
            except Exception as error:
                self.connected_event.clear()
                logger.warning("BLE connection interrupted: %s", error)
            if connected_at is not None and asyncio.get_running_loop().time() - connected_at >= 30:
                backoff = 1.0
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2.0, 60.0)
