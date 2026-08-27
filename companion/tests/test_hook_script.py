import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[2] / ".codex" / "hooks" / "codex_display_event.py"
SPEC = importlib.util.spec_from_file_location("codex_display_event", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class HookScriptTests(unittest.TestCase):
    def test_token_delta_reads_complete_matching_utc_records_only(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "rollout.jsonl"
            records = [
                {"timestamp": "2026-08-27T01:00:00Z", "type": "event_msg",
                 "payload": {"type": "token_count", "info": {
                     "last_token_usage": {"total_tokens": 123}}}},
                {"timestamp": "2026-08-26T01:00:00Z", "type": "event_msg",
                 "payload": {"type": "token_count", "info": {
                     "last_token_usage": {"total_tokens": 999}}}},
            ]
            path.write_text("\n".join(map(json.dumps, records)) + "\n", encoding="utf-8")
            tokens, offset = MODULE.token_delta(str(path), 0, "2026-08-27")
            self.assertEqual(tokens, 123)
            self.assertEqual(offset, path.stat().st_size)
