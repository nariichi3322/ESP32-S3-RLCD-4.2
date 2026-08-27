import json
import tempfile
import unittest
from datetime import date
from pathlib import Path

from companion.codex_display.metrics import build_snapshot, rollout_is_active


class MetricsTests(unittest.TestCase):
    def test_builds_account_snapshot_and_utc_buckets(self):
        limits = {
            "rateLimitsByLimitId": {"codex": {"primary": {
                "usedPercent": 35, "windowDurationMins": 300, "resetsAt": 1600}}},
            "rateLimitResetCredits": {"availableCount": 2, "credits": [
                {"status": "available", "expiresAt": 1300}]}}
        usage = {"dailyUsageBuckets": [
            {"startDate": "2026-08-27", "tokens": 100},
            {"startDate": "2026-08-26", "tokens": 40}]}
        value = build_snapshot(limits, usage, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertEqual(value.remaining_percent, 65)
        self.assertEqual(value.quota_reset_seconds, 600)
        self.assertEqual(value.tokens_today, 100)
        self.assertFalse(value.tokens_today_estimated)
        self.assertEqual(value.tokens_7d, 140)
        self.assertEqual(value.next_credit_expiry_seconds, 300)

    def test_rollout_uses_latest_boundary(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "rollout.jsonl"
            path.write_text("\n".join([
                json.dumps({"type": "event_msg", "payload": {"type": "task_started"}}),
                json.dumps({"type": "event_msg", "payload": {"type": "task_complete"}}),
            ]) + "\n", encoding="utf-8")
            self.assertFalse(rollout_is_active(str(path)))
