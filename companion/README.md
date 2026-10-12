# Codex / Claude Usage Windows Companion

> **Language:** English (current) · [繁體中文](README_zh_TW.md)

This Windows-only companion sends AI usage to the display's **Codex** and
**Claude** pages over one encrypted BLE link:

- **Codex**: starts the local `codex app-server`, reads real account rate
  limits and UTC daily usage, and reconciles active rollouts.
- **Claude Code**: 5-hour / 7-day rate limits, today's and 7-day token totals,
  and the number of running sessions.

Either one may be missing: without Codex the companion runs Claude-only, and
without Claude Code setup the Claude page shows `--`. It has no device-command
channel and never copies Codex or Claude credentials to the ESP32.

## Requirements

- Windows 10/11 with Bluetooth LE enabled.
- Python **3.10+** ([python.org](https://www.python.org/), tick "Add python.exe
  to PATH").
- Display firmware with the Claude page (older firmware has no Claude
  characteristic; the companion then sends Codex only).
- For Codex: an authenticated Codex CLI or the OpenAI ChatGPT/Codex VS Code
  extension. Override binary discovery with `CODEX_BIN` when needed.
- For Claude: Claude Code (CLI or VS Code extension) signed in. Rate limits are
  reported only for **Pro/Max** subscriptions; token totals work for any login.

## Quick start on a new PC

Run every command from the repository root in a normal, **non-elevated**
PowerShell.

1. **Get the code.** Keep the folder where it is afterwards: the installers
   write absolute paths into your settings (moving it means re-running step 4).

   ```powershell
   git clone https://github.com/nariichi3322/ESP32-S3-RLCD-4.2.git
   cd ESP32-S3-RLCD-4.2
   ```

2. **Create the environment and check the data** (no Bluetooth needed):

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\run.ps1 --once
   ```

   The first run creates `companion\.venv` and installs `bleak`. It prints one
   v3 JSON line for Codex (when available) and one for Claude.

3. **Pair once.** Switch the display to the **Codex or Claude page**; Bluetooth
   only runs on those two pages. Then:

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\run.ps1
   ```

   Enter the six-digit passkey shown on the display in the Windows dialog. When
   the log shows `Codex Display LINKED`, press `Ctrl+C`: only one companion can
   run at a time, and step 5 starts its own.

4. **Install the Claude Code integration** (skip if you do not use Claude):

   ```powershell
   companion\.venv\Scripts\python.exe companion\install_claude.py
   ```

   Restart VS Code / Claude Code afterwards so the new hooks and mod load.
   Codex users may also install the Codex hooks (see
   [Codex lifecycle hook](#codex-lifecycle-hook-and-local-token-estimate)):

   ```powershell
   companion\.venv\Scripts\python.exe companion\install_hook.py
   ```

5. **Start automatically at logon:**

   ```powershell
   powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1
   powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Status
   ```

6. **Check it.** The log is `%LOCALAPPDATA%\CodexUsageDisplay\companion.log`.
   After Claude Code answers one message,
   `%LOCALAPPDATA%\CodexUsageDisplay\claude-rate-limits.json` appears and keeps
   updating; the Claude page shows the remaining percentages within about
   15 seconds.

Uninstall everything with:

```powershell
powershell -ExecutionPolicy Bypass -File companion\install_windows_task.ps1 -Uninstall
companion\.venv\Scripts\python.exe companion\install_claude.py --uninstall
companion\.venv\Scripts\python.exe companion\install_hook.py --uninstall
```

## Pairing and BLE behaviour

`--once` prints the actual v3 JSON without BLE; the firmware also accepts
legacy v1/v2 payloads. A snapshot is sent immediately after connection, every
15 seconds as a heartbeat, and immediately after lifecycle state changes. Full
app-server metrics refresh every 60 seconds. Scan/reconnect failures use
bounded exponential backoff. Claude data uses a second characteristic
(`CLAUDE_STATUS_UUID`, same v3 JSON) in the same service and pairing.

If pairing must be replaced, use the display's local system setting
`清除配對` (or `清除配对` in Simplified Chinese), remove stale Windows Bluetooth
entries if present, and pair again. BLE initialization failure does not affect
the clock or Wi-Fi pages.

If scanning fails, verify Bluetooth is enabled in Windows Settings, remove any
stale `Codex Display` device, and confirm organization privacy policy permits
Bluetooth access for desktop applications. Windows does not expose a separate
per-terminal Bluetooth permission prompt like macOS.

## Codex lifecycle hook and local token estimate

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

## Claude Code integration

`install_claude.py` edits `%USERPROFILE%\.claude\settings.json` (or
`%CLAUDE_CONFIG_DIR%`), writes a timestamped `settings.json.bak-*` first, and
preserves unrelated settings. It installs three things:

- **`statusLine`** → `claude_display_event.py --statusline`, which stores the
  rate limits when Claude Code runs in a terminal. A previous statusLine keeps
  running (chained) and is restored exactly on uninstall.
- **`rlcd-usage-bridge` mod** (`companion\rlcd-usage-bridge`, registered as the
  local marketplace `rlcd-local`). The VS Code extension never runs
  `statusLine`; this mod writes the same rate-limit file from each session's own
  measurement in the terminal, desktop and VS Code alike. It uses Claude Code's
  early-access plugin API, so a Claude Code update may require adapting it.
- **`UserPromptSubmit` / `Stop` / `SessionEnd` hooks**, which track running
  sessions.

Stored under `%LOCALAPPDATA%\CodexUsageDisplay`: `claude-rate-limits.json`
(5-hour and 7-day used percentage and reset time) and `claude-sessions.json`
(session ids and timestamps of running turns; never prompt text). Rate limits
appear only after the first API response of a Claude Code session; without them
the display shows the window as unavailable. Token totals are summed from
`projects/**/*.jsonl` transcripts (input + output + cache creation + cache
read, deduplicated per message, local calendar day and last 7 local days). A
session counts as running for at most 6 hours.

## Start automatically at logon

The per-user Scheduled Task created by `install_windows_task.ps1` runs hidden
through `pythonw.exe`, starts at logon, retries failures, may run on battery,
and writes a rotating 1 MiB log under `%LOCALAPPDATA%\CodexUsageDisplay`. It
only runs the companion; the Claude/Codex integrations are installed separately
(step 4 above).

## Tests

```powershell
python -m unittest discover -s companion\tests -v
```

The Codex app-server interface, rollout format and Claude Code transcript
format are local integration points, not long-term public APIs; an update may
require parser adaptation.
