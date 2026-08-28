import json
import tempfile
import unittest
from datetime import date
from pathlib import Path

from companion.codex_display.metrics import build_snapshot, rollout_is_active


class MetricsTests(unittest.TestCase):
    def test_builds_account_snapshot_and_utc_buckets(self):
        limits = {
            "rateLimitsByLimitId": {"codex": {
                "primary": {"usedPercent": 35, "windowDurationMins": 300, "resetsAt": 1600},
                "secondary": {"usedPercent": 20, "windowDurationMins": 43200, "resetsAt": 2200},
                "credits": {"balance": "812.75", "hasCredits": True,
                            "unlimited": False}}},
            "rateLimitResetCredits": {"availableCount": 2, "credits": [
                {"status": "available", "expiresAt": 1800},
                {"status": "used", "expiresAt": 1100},
                {"status": "available", "expiresAt": 1300}]}}
        usage = {"dailyUsageBuckets": [
            {"startDate": "2026-08-27", "tokens": 100},
            {"startDate": "2026-08-26", "tokens": 40}]}
        value = build_snapshot(limits, usage, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertEqual(value.remaining_percent, 65)
        self.assertEqual(value.quota_reset_seconds, 600)
        self.assertTrue(value.secondary_available)
        self.assertEqual(value.secondary_remaining_percent, 80)
        # The companion forwards the service-provided duration verbatim.  A
        # free account can therefore show 30d instead of being mislabeled 7d.
        self.assertEqual(value.secondary_limit_window_minutes, 43200)
        self.assertEqual(value.secondary_quota_reset_seconds, 1200)
        self.assertEqual(value.tokens_today, 100)
        self.assertFalse(value.tokens_today_estimated)
        self.assertEqual(value.tokens_7d, 140)
        self.assertTrue(value.paid_credits_available)
        self.assertFalse(value.paid_credits_unlimited)
        self.assertEqual(value.paid_credits_balance, 812)
        self.assertEqual(value.next_credit_expiry_seconds, 300)
        self.assertEqual(value.reset_credits, 2)

    def test_paid_credits_fallback_and_states(self):
        cases = [
            ({"balance": "0", "hasCredits": False, "unlimited": False},
             (True, False, 0)),
            ({"balance": None, "hasCredits": False, "unlimited": False},
             (True, False, 0)),
            ({"balance": None, "hasCredits": True, "unlimited": True},
             (True, True, 0)),
            (None, (False, False, 0)),
            ({"balance": "invalid", "hasCredits": True, "unlimited": False},
             (False, False, 0)),
            ({"balance": "4294967296", "hasCredits": True, "unlimited": False},
             (False, False, 0)),
        ]
        for credits, expected in cases:
            with self.subTest(credits=credits):
                limits = {"rateLimits": {"primary": {}, "credits": credits}}
                value = build_snapshot(limits, {"dailyUsageBuckets": []}, [], now=1000,
                                       utc_date=date(2026, 8, 27), utc_offset_minutes=480)
                self.assertEqual((value.paid_credits_available,
                                  value.paid_credits_unlimited,
                                  value.paid_credits_balance), expected)

    def test_codex_paid_credits_take_precedence_over_legacy(self):
        limits = {
            "rateLimitsByLimitId": {"codex": {
                "credits": {"balance": "12", "hasCredits": True, "unlimited": False}}},
            "rateLimits": {"credits": {
                "balance": "99", "hasCredits": True, "unlimited": False}}}
        value = build_snapshot(limits, {"dailyUsageBuckets": []}, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertEqual(value.paid_credits_balance, 12)

    def test_missing_reset_credit_expiry_is_zero(self):
        limits = {"rateLimitResetCredits": {
            "availableCount": 0, "credits": [{"status": "used", "expiresAt": 1200}]}}
        value = build_snapshot(limits, {"dailyUsageBuckets": []}, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertEqual(value.reset_credits, 0)
        self.assertEqual(value.next_credit_expiry_seconds, 0)

    def test_missing_secondary_is_not_inferred_from_week_tokens(self):
        limits = {"rateLimits": {"primary": {
            "usedPercent": 10, "windowDurationMins": 300, "resetsAt": 1600},
            "secondary": None}}
        usage = {"dailyUsageBuckets": [
            {"startDate": "2026-08-27", "tokens": 999999}]}
        value = build_snapshot(limits, usage, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertFalse(value.secondary_available)
        self.assertEqual(value.secondary_remaining_percent, 0)
        self.assertEqual(value.secondary_limit_window_minutes, 0)
        self.assertEqual(value.secondary_quota_reset_seconds, 0)
        self.assertEqual(value.tokens_7d, 999999)

    def test_incomplete_secondary_is_unavailable(self):
        limits = {"rateLimits": {
            "primary": {"usedPercent": 10, "windowDurationMins": 300, "resetsAt": 1600},
            "secondary": {"usedPercent": 20, "windowDurationMins": 43200}}}
        value = build_snapshot(limits, {"dailyUsageBuckets": []}, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertFalse(value.secondary_available)
        self.assertEqual(value.secondary_remaining_percent, 0)
        self.assertEqual(value.secondary_limit_window_minutes, 0)
        self.assertEqual(value.secondary_quota_reset_seconds, 0)

    def test_secondary_falls_back_to_legacy_rate_limits(self):
        limits = {
            "rateLimitsByLimitId": {"codex": {
                "primary": {"usedPercent": 10, "windowDurationMins": 300,
                            "resetsAt": 1600}}},
            "rateLimits": {
                "secondary": {"usedPercent": 25, "windowDurationMins": 43200,
                              "resetsAt": 2600}}}
        value = build_snapshot(limits, {"dailyUsageBuckets": []}, [], now=1000,
                               utc_date=date(2026, 8, 27), utc_offset_minutes=480)
        self.assertTrue(value.secondary_available)
        self.assertEqual(value.secondary_remaining_percent, 75)
        self.assertEqual(value.secondary_limit_window_minutes, 43200)
        self.assertEqual(value.secondary_quota_reset_seconds, 1600)

    def test_rollout_uses_latest_boundary(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "rollout.jsonl"
            path.write_text("\n".join([
                json.dumps({"type": "event_msg", "payload": {"type": "task_started"}}),
                json.dumps({"type": "event_msg", "payload": {"type": "task_complete"}}),
            ]) + "\n", encoding="utf-8")
            self.assertFalse(rollout_is_active(str(path)))
