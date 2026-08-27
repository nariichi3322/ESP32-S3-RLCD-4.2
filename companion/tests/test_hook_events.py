import asyncio
import json
import tempfile
import unittest
from pathlib import Path

from companion.codex_display.hook_events import HookActivityTracker


class HookEventTests(unittest.IsolatedAsyncioTestCase):
    async def test_prompt_counts_and_matching_complete_removes(self):
        changed = 0

        async def on_change():
            nonlocal changed
            changed += 1

        with tempfile.TemporaryDirectory() as root:
            rollout = Path(root) / "rollout.jsonl"
            rollout.write_text(json.dumps({"type": "event_msg", "payload": {
                "type": "task_started", "turn_id": "turn"}}) + "\n", encoding="utf-8")
            tracker = HookActivityTracker(on_change, Path(root) / "events")
            event = {"event": "UserPromptSubmit", "session_id": "session",
                     "turn_id": "turn", "transcript_path": str(rollout), "at": 1}
            await tracker.process_event(event)
            self.assertEqual(tracker.extra_count(set()), 1)
            rollout.write_text(json.dumps({"type": "event_msg", "payload": {
                "type": "task_complete", "turn_id": "turn"}}) + "\n", encoding="utf-8")
            await tracker.reconcile(now=2)
            self.assertEqual(tracker.extra_count(set()), 0)
            self.assertEqual(changed, 2)
