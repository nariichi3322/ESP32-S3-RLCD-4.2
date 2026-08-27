# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import asyncio
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any, Optional


class AppServerError(RuntimeError):
    pass


def _bundled_windows_codex() -> Optional[str]:
    roots = []
    profile = os.environ.get("USERPROFILE")
    program_files = os.environ.get("ProgramFiles")
    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    portable = os.environ.get("VSCODE_PORTABLE")
    if profile:
        roots += [Path(profile) / ".vscode" / "extensions",
                  Path(profile) / ".vscode-insiders" / "extensions"]
    for install_root in (program_files, program_files_x86):
        if install_root:
            roots += [Path(install_root) / "Visual Studio Code" / "data" / "extensions",
                      Path(install_root) / "Microsoft VS Code" / "data" / "extensions"]
    if portable:
        roots.append(Path(portable) / "data" / "extensions")
    candidates = [item for root in roots if root.is_dir()
                  for item in root.glob("openai.chatgpt-*/bin/windows-*/codex.exe")]
    return str(max(candidates, key=lambda item: item.stat().st_mtime)) if candidates else None


def find_codex_binary() -> str:
    configured = os.environ.get("CODEX_BIN")
    found = configured or shutil.which("codex") or _bundled_windows_codex()
    if not found:
        raise AppServerError(
            "Codex CLI not found; set CODEX_BIN or install the Codex CLI/VS Code extension")
    return found


class AppServerClient:
    def __init__(self, codex_binary: Optional[str] = None) -> None:
        self._binary = codex_binary or find_codex_binary()
        self._process: Optional[asyncio.subprocess.Process] = None
        self._reader: Optional[asyncio.Task] = None
        self._pending: dict[int, asyncio.Future] = {}
        self._next_id = 1

    async def start(self) -> None:
        if self._process is not None:
            return
        options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        self._process = await asyncio.create_subprocess_exec(
            self._binary, "app-server", stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
            limit=2 * 1024 * 1024, **options)
        self._reader = asyncio.create_task(self._read_loop())
        await self.request("initialize", {
            "clientInfo": {"name": "codex_usage_display",
                           "title": "Codex Usage Display", "version": "0.1.0"},
            "capabilities": {"experimentalApi": True}})
        await self.notify("initialized", {})

    async def stop(self) -> None:
        process, self._process = self._process, None
        if process is not None and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), 3)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
        if self._reader is not None:
            self._reader.cancel()
            await asyncio.gather(self._reader, return_exceptions=True)
            self._reader = None

    async def wait_for_exit(self) -> None:
        if self._reader is None:
            raise AppServerError("Codex app-server has not started")
        await self._reader
        raise AppServerError("Codex app-server exited")

    async def request(self, method: str, params: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        future = asyncio.get_running_loop().create_future()
        self._pending[request_id] = future
        message: dict[str, Any] = {"method": method, "id": request_id}
        if params is not None:
            message["params"] = params
        await self._send(message)
        try:
            return await asyncio.wait_for(future, 20)
        finally:
            self._pending.pop(request_id, None)

    async def notify(self, method: str, params: dict[str, Any]) -> None:
        await self._send({"method": method, "params": params})

    async def _send(self, message: dict[str, Any]) -> None:
        if self._process is None or self._process.stdin is None:
            raise AppServerError("Codex app-server has not started")
        self._process.stdin.write((json.dumps(message, separators=(",", ":")) + "\n").encode())
        await self._process.stdin.drain()

    async def _read_loop(self) -> None:
        assert self._process is not None and self._process.stdout is not None
        while line := await self._process.stdout.readline():
            try:
                message = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            future = self._pending.get(message.get("id"))
            if future is None or future.done():
                continue
            if "error" in message:
                error = message["error"]
                future.set_exception(AppServerError(
                    f"{error.get('code')}: {error.get('message')}"))
            else:
                future.set_result(message.get("result", {}))
        error = AppServerError("Codex app-server exited")
        for future in self._pending.values():
            if not future.done():
                future.set_exception(error)
