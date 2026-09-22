import asyncio
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from companion.codex_display.app_server import (
    _INITIALIZE_TIMEOUT_SECONDS,
    AppServerClient,
    find_codex_binary,
)


class AppServerTests(unittest.TestCase):
    def test_configured_binary_has_priority(self):
        with patch.dict(os.environ, {"CODEX_BIN": r"C:\tools\codex.exe"}):
            self.assertEqual(find_codex_binary(), r"C:\tools\codex.exe")

    def test_finds_codex_bundled_in_system_vscode_data_directory(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            binary = (Path(temporary_directory) / "Visual Studio Code" / "data" /
                      "extensions" / "openai.chatgpt-test" / "bin" /
                      "windows-x86_64" / "codex.exe")
            binary.parent.mkdir(parents=True)
            binary.touch()
            environment = {
                "ProgramFiles": temporary_directory,
                "USERPROFILE": str(Path(temporary_directory) / "user"),
            }
            with patch.dict(os.environ, environment, clear=True), \
                    patch("companion.codex_display.app_server.shutil.which",
                          return_value=None):
                self.assertEqual(find_codex_binary(), str(binary))


class _FakeStream:
    async def readline(self):
        await asyncio.Future()


class _FakeWriter:
    def write(self, _data):
        pass

    async def drain(self):
        pass


class _FakeProcess:
    def __init__(self):
        self.stdin = _FakeWriter()
        self.stdout = _FakeStream()
        self.returncode = None
        self.terminated = False

    def terminate(self):
        self.terminated = True
        self.returncode = 0

    def kill(self):
        self.terminated = True
        self.returncode = -9

    async def wait(self):
        return self.returncode


class AppServerLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_initialize_timeout_restarts_process_then_succeeds(self):
        client = AppServerClient("codex.exe")
        first_process, second_process = _FakeProcess(), _FakeProcess()

        with patch("companion.codex_display.app_server.asyncio.create_subprocess_exec",
                   new=AsyncMock(side_effect=[first_process, second_process])) as spawn, \
                patch("companion.codex_display.app_server.asyncio.sleep",
                      new=AsyncMock()) as sleep, \
                patch.object(client, "request", new=AsyncMock(
                    side_effect=[asyncio.TimeoutError(), {}])) as request, \
                patch.object(client, "notify", new=AsyncMock()):
            await client.start()
            await client.stop()

        self.assertEqual(spawn.await_count, 2)
        self.assertTrue(first_process.terminated)
        self.assertTrue(second_process.terminated)
        self.assertEqual(request.await_args_list[0].kwargs["timeout_seconds"],
                         _INITIALIZE_TIMEOUT_SECONDS)
        sleep.assert_awaited_once_with(2)
