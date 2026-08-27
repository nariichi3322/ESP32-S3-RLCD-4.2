# Adapted from codex-usage-display (MIT); see THIRD_PARTY_NOTICES.md
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from .paths import USAGE_PATH


def read_local_tokens(path: Path = USAGE_PATH,
                      utc_date: Optional[str] = None) -> Optional[int]:
    utc_date = utc_date or datetime.now(timezone.utc).date().isoformat()
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        if value.get("utc_date") != utc_date:
            return None
        return max(0, int(value.get("tokens", 0)))
    except (OSError, ValueError, TypeError, AttributeError):
        return None
