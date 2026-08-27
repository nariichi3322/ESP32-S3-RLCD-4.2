# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import os
from pathlib import Path


def state_directory() -> Path:
    root = os.environ.get("LOCALAPPDATA")
    if not root:
        root = str(Path.home() / "AppData" / "Local")
    return Path(root) / "CodexUsageDisplay"


STATE_DIR = state_directory()
EVENTS_PATH = STATE_DIR / "hook-events.jsonl"
USAGE_PATH = STATE_DIR / "local-usage.json"
LOCK_PATH = STATE_DIR / "hook-events.lock"
