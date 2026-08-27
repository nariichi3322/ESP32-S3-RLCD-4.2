import json
import tempfile
import unittest
from pathlib import Path

from companion.codex_display.local_usage import read_local_tokens


class LocalUsageTests(unittest.TestCase):
    def test_reads_only_matching_utc_date(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "usage.json"
            path.write_text(json.dumps({"utc_date": "2026-08-27", "tokens": 42}),
                            encoding="utf-8")
            self.assertEqual(read_local_tokens(path, "2026-08-27"), 42)
            self.assertIsNone(read_local_tokens(path, "2026-08-28"))

    def test_invalid_state_is_ignored(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "usage.json"
            path.write_text("not-json", encoding="utf-8")
            self.assertIsNone(read_local_tokens(path, "2026-08-27"))
