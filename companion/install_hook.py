"""Idempotently install/remove Codex Usage lifecycle hooks on Windows."""
# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

EVENTS = ("UserPromptSubmit", "Stop")
SCRIPT_NAME = "codex_display_event.py"


def belongs(group: object) -> bool:
    return isinstance(group, dict) and any(
        isinstance(item, dict) and SCRIPT_NAME in str(item.get("command", ""))
        for item in group.get("hooks", []))


def updated_config(config: dict, script: Path, uninstall: bool,
                   python: Path = Path(sys.executable)) -> dict:
    result = dict(config)
    hooks = dict(result.get("hooks") or {})
    command = f'"{python}" "{script}"'
    for event in EVENTS:
        groups = hooks.get(event) or []
        if not isinstance(groups, list):
            raise ValueError(f"hooks.{event} must be a list")
        groups = [group for group in groups if not belongs(group)]
        if not uninstall:
            groups.append({"hooks": [{"type": "command", "command": command, "timeout": 2}]})
        if groups:
            hooks[event] = groups
        else:
            hooks.pop(event, None)
    result["hooks"] = hooks
    return result


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="hooks.", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    config_path = Path.home() / ".codex" / "hooks.json"
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        config = {}
    script = Path(__file__).resolve().parents[1] / ".codex" / "hooks" / SCRIPT_NAME
    write(config_path, updated_config(config, script, args.uninstall))
    print(("Removed" if args.uninstall else "Installed") + f" hooks in {config_path}")


if __name__ == "__main__":
    main()
