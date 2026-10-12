import asyncio
import unittest
from unittest.mock import AsyncMock, patch

from companion.codex_display.ble import (
    BleCompanion,
    _characteristic_missing,
    _client_options,
    _enable_winrt_service_changed_retry,
)
from companion.codex_display.constants import CLAUDE_STATUS_UUID, STATUS_UUID


class FakeClient:
    def __init__(self):
        self.calls = []
        self.is_connected = True

    async def write_gatt_char(self, uuid, value, response):
        self.calls.append((uuid, value, response))


class Services:
    def __init__(self, present):
        self.present = present

    def get_characteristic(self, uuid):
        return object() if self.present else None


async def claude_payload():
    return b"claude"


class BleTests(unittest.IsolatedAsyncioTestCase):
    def test_characteristic_discovery_error_is_retryable(self):
        self.assertTrue(_characteristic_missing(
            RuntimeError("Characteristic 1234 was not found!")))
        self.assertFalse(_characteristic_missing(
            RuntimeError("Codex Display not found")))

    def test_windows_client_leaves_pairing_to_display_only_peripheral(self):
        callback = lambda _: None
        self.assertEqual(_client_options(callback), {
            "timeout": 60,
            "disconnected_callback": callback,
            "winrt": {"use_cached_services": False},
        })

    def test_enables_installed_winrt_service_changed_discovery_retry(self):
        class Backend:
            _retry_on_services_changed = False

        class Client:
            _backend = Backend()

        client = Client()
        self.assertTrue(_enable_winrt_service_changed_retry(client))
        self.assertTrue(client._backend._retry_on_services_changed)

    def test_service_changed_retry_is_safe_for_unknown_backend(self):
        self.assertFalse(_enable_winrt_service_changed_retry(object()))

    async def test_send_uses_only_status_write_with_response(self):
        async def payload():
            return b"status"

        client = FakeClient()
        await BleCompanion(payload)._send(client)
        self.assertEqual(client.calls, [(STATUS_UUID, b"status", True)])

    async def test_initial_send_prioritizes_cached_snapshot(self):
        async def fresh_payload():
            return b"fresh"

        async def cached_payload():
            return b"cached"

        client = FakeClient()
        companion = BleCompanion(
            fresh_payload, initial_snapshot_provider=cached_payload)
        await companion._send_after_authentication(client)
        self.assertEqual(client.calls, [(STATUS_UUID, b"cached", True)])

    async def test_initial_send_waits_for_passkey_authentication(self):
        async def payload():
            return b"status"

        client = FakeClient()
        client.write_gatt_char = AsyncMock(side_effect=[
            RuntimeError("Protocol Error 0x05: Insufficient Authentication"),
            None,
        ])
        companion = BleCompanion(payload)
        with patch("companion.codex_display.ble.asyncio.sleep",
                   AsyncMock()) as sleep:
            await companion._send_after_authentication(client)
        self.assertEqual(client.write_gatt_char.await_count, 2)
        sleep.assert_awaited_once()

    async def test_initial_send_does_not_hide_non_authentication_errors(self):
        async def payload():
            return b"status"

        client = FakeClient()
        client.write_gatt_char = AsyncMock(
            side_effect=RuntimeError("Characteristic not found"))
        with self.assertRaisesRegex(RuntimeError, "Characteristic not found"):
            await BleCompanion(payload)._send_after_authentication(client)

    async def test_missing_device_uses_bounded_reconnect_backoff(self):
        async def payload():
            return b"status"

        companion = BleCompanion(payload)
        companion.find_device = AsyncMock(return_value=None)
        with patch("companion.codex_display.ble.platform.system", return_value="Windows"), \
             patch("companion.codex_display.ble.asyncio.sleep",
                   AsyncMock(side_effect=asyncio.CancelledError)) as sleep:
            with self.assertRaises(asyncio.CancelledError):
                await companion.run_forever()
        sleep.assert_awaited_once_with(1.0)

    async def test_codex_then_claude_written_when_characteristic_present(self):
        async def payload():
            return b"codex"

        client = FakeClient()
        client.services = Services(True)
        companion = BleCompanion(payload, claude_provider=claude_payload)
        await companion._send_all(client)
        self.assertEqual(client.calls, [(STATUS_UUID, b"codex", True),
                                        (CLAUDE_STATUS_UUID, b"claude", True)])

    async def test_missing_claude_characteristic_is_skipped_and_logged_once(self):
        async def payload():
            return b"codex"

        client = FakeClient()
        client.services = Services(False)
        companion = BleCompanion(payload, claude_provider=claude_payload)
        with self.assertLogs("companion.codex_display.ble", "INFO") as logs:
            await companion._send_all(client)
            await companion._send_all(client)
        self.assertEqual(client.calls, [(STATUS_UUID, b"codex", True)] * 2)
        self.assertEqual(len(logs.output), 1)

    async def test_claude_only_initial_write_retries_authentication(self):
        client = FakeClient()
        client.services = Services(True)
        client.write_gatt_char = AsyncMock(side_effect=[
            RuntimeError("Protocol Error 0x05: Insufficient Authentication"), None])
        companion = BleCompanion(None, claude_provider=claude_payload)
        with patch("companion.codex_display.ble.asyncio.sleep", AsyncMock()):
            await companion._send_after_authentication(client)
        self.assertEqual(client.write_gatt_char.await_count, 2)
        self.assertEqual(client.write_gatt_char.await_args.args[0], CLAUDE_STATUS_UUID)
