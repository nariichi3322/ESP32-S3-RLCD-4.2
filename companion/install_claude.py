"""Idempotently install/remove the Claude Code statusLine and lifecycle hooks."""
import argparse
import json
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

EVENTS = ("UserPromptSubmit", "Stop", "SessionEnd")
SCRIPT_NAME = "claude_display_event.py"
PREVIOUS_NAME = "claude-display-previous-statusline.json"
# The VS Code extension never runs statusLine; this mod writes the same
# rate-limit file from the session's own measurements, on every surface.
MOD_DIR = "rlcd-usage-bridge"
MARKETPLACE = "rlcd-local"
PLUGIN = f"{MOD_DIR}@{MARKETPLACE}"


def belongs(group: object) -> bool:
    return isinstance(group, dict) and any(
        isinstance(item, dict) and SCRIPT_NAME in str(item.get("command", ""))
        for item in group.get("hooks", []))


def ours(status_line: object) -> bool:
    return isinstance(status_line, dict) and SCRIPT_NAME in str(status_line.get("command", ""))


def updated_settings(settings: dict, script: Path, python: Path, previous_path: Path,
                     uninstall: bool, previous: dict | None) -> tuple[dict, dict | None]:
    """Return (new settings, statusLine to persist or None). `previous` is the saved wrapper."""
    result = dict(settings)
    hooks = dict(result.get("hooks") or {})
    for event in EVENTS:
        groups = hooks.get(event) or []
        if not isinstance(groups, list):
            raise ValueError(f"hooks.{event} must be a list")
        groups = [group for group in groups if not belongs(group)]
        if not uninstall:
            groups.append({"hooks": [{"type": "command", "timeout": 2,
                                      "command": f'"{python}" "{script}" --hook'}]})
        if groups:
            hooks[event] = groups
        else:
            hooks.pop(event, None)
    if hooks:
        result["hooks"] = hooks
    else:
        result.pop("hooks", None)
    markets = dict(result.get("extraKnownMarketplaces") or {})
    plugins = dict(result.get("enabledPlugins") or {})
    markets.pop(MARKETPLACE, None)
    plugins.pop(PLUGIN, None)
    if not uninstall:
        markets[MARKETPLACE] = {"source": {"source": "directory",
                                           "path": str(script.parent / MOD_DIR)}}
        plugins[PLUGIN] = True
    for key, value in (("extraKnownMarketplaces", markets), ("enabledPlugins", plugins)):
        if value:
            result[key] = value
        else:
            result.pop(key, None)
    current = result.get("statusLine")
    if uninstall:
        if ours(current):
            saved = (previous or {}).get("statusLine")
            if saved is None:
                result.pop("statusLine", None)
            else:
                result["statusLine"] = saved
        return result, None
    if not ours(current):
        previous = {"statusLine": current}  # None when the user had no statusLine
    chain = f' --chain-file "{previous_path}"' if (previous or {}).get("statusLine") else ""
    result["statusLine"] = {"type": "command", "command": f'"{python}" "{script}" --statusline{chain}'}
    return result, previous


def write_json(path: Path, value: dict) -> None:
    fd, temporary = tempfile.mkstemp(prefix="settings.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def install(claude_dir: Path, script: Path, python: Path, uninstall: bool) -> Path:
    settings_path, previous_path = claude_dir / "settings.json", claude_dir / PREVIOUS_NAME
    try:
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
        if not isinstance(settings, dict):
            raise ValueError("settings.json must contain an object")
    except FileNotFoundError:
        settings = {}
    try:
        previous = json.loads(previous_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        previous = None
    result, previous = updated_settings(settings, script, python, previous_path, uninstall, previous)
    claude_dir.mkdir(parents=True, exist_ok=True)
    if settings_path.exists() and result != settings:
        shutil.copy2(settings_path, settings_path.with_name(
            f"settings.json.bak-{time.strftime('%Y%m%d-%H%M%S')}"))
    # Persist the previous statusLine first so a crash can never lose it.
    if previous is not None:
        write_json(previous_path, previous)
    write_json(settings_path, result)
    if uninstall:
        previous_path.unlink(missing_ok=True)
    return settings_path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    claude_dir = Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude")
    script = Path(__file__).resolve().with_name(SCRIPT_NAME)
    path = install(claude_dir, script, Path(sys.executable), args.uninstall)
    print(("Removed" if args.uninstall else "Installed") + f" Claude Code display hooks in {path}")


if __name__ == "__main__":
    main()
