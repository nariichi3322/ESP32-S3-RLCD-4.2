# Codex Usage Windows Companion

This Windows-only companion starts the local `codex app-server`, reads real
account rate limits and UTC daily usage, reconciles active rollouts, and sends a
compact status snapshot to the display over one encrypted BLE characteristic.
It has no device-command channel and never copies Codex credentials to ESP32.

Requirements: Windows 10/11, Python 3.9+, Bluetooth LE, and an authenticated
Codex CLI or the OpenAI ChatGPT/Codex VS Code extension. Override binary
discovery with `CODEX_BIN` when needed.

## First run and pairing

```powershell
powershell -ExecutionPolicy Bypass -File companion\run.ps1 --once
powershell -ExecutionPolicy Bypass -File companion\run.ps1
```

`--once` prints the actual v1 JSON without BLE. On the first normal run Windows
opens a pairing dialog. Enter the six-digit passkey shown on the display. A
snapshot is sent immediately after connection, every 15 seconds as a heartbeat,
and immediately after lifecycle state changes. Full app-server metrics refresh
every 60 seconds. Scan/reconnect failures use bounded exponential backoff.

If pairing must be replaced, use the display's local system setting
`清除 Codex 配对`, remove stale Windows Bluetooth entries if present, and pair
again. BLE initialization failure does not affect the clock or Wi-Fi pages.

If scanning fails, verify Bluetooth is enabled in Windows Settings, remove any
stale `Codex Display` device, and confirm organization privacy policy permits
Bluetooth access for desktop applications. Windows does not expose a separate
per-terminal Bluetooth permission prompt like macOS.

## Lifecycle hook and local token estimate

```powershell
python companion\install_hook.py
python .codex\hooks\codex_display_event.py --bootstrap
```

The idempotent installer preserves unrelated entries in
`%USERPROFILE%\.codex\hooks.json`. `UserPromptSubmit` and `Stop` append only
session/turn/path metadata to
`%LOCALAPPDATA%\CodexUsageDisplay\hook-events.jsonl`; prompt and response text
are never copied. `Stop` incrementally reads token-count records from the
referenced rollout and atomically maintains `local-usage.json`. The local value
is displayed with `~` only while the official UTC daily bucket is unavailable.
The queue rotates at 1 MiB and keeps one archive.

Uninstall hooks:

```powershell
python companion\install_hook.py --uninstall
```

## Start automatically at logon

Run from a normal, non-elevated PowerShell session:

```powershell
powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1
powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Status
```

The per-user Scheduled Task runs hidden through `pythonw.exe`, starts at logon,
retries failures, may run on battery, and writes a rotating 1 MiB log under
`%LOCALAPPDATA%\CodexUsageDisplay`. Uninstall it with:

```powershell
powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Uninstall
```

## Tests

```powershell
python -m unittest discover -s companion\tests -v
```

The Codex app-server interface and rollout format are local integration points,
not long-term public APIs; a Codex update may require parser adaptation.
