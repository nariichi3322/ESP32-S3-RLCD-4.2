import json
import os
import tempfile
import time
import unittest
from datetime import datetime
from pathlib import Path

from companion.codex_display.claude_usage import (
    ClaudeSnapshotCache, TokenScanner, build_claude_snapshot, running_sessions)
from companion.codex_display.protocol import encode_snapshot

NOW = 1_800_000_000


def record(stamp, msg, req, tokens=(1, 2, 3, 4)):
    usage = dict(zip(("input_tokens", "output_tokens", "cache_creation_input_tokens",
                      "cache_read_input_tokens"), tokens))
    return json.dumps({"type": "assistant", "timestamp": datetime.fromtimestamp(stamp).astimezone()
                       .isoformat(), "requestId": req, "message": {"id": msg, "usage": usage}})


class MappingTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.addCleanup(self.dir.cleanup)

    def build(self, limits, sessions=None, detail=None):
        rate, sess, detail_path = self.root / "r.json", self.root / "s.json", self.root / "d.json"
        if limits is not None:
            rate.write_text(json.dumps(limits))
        if sessions is not None:
            sess.write_text(json.dumps({"sessions": sessions}))
        if detail is not None:
            detail_path.write_text(json.dumps(detail))
        return build_claude_snapshot((5, 9), rate, sess, NOW, detail_path)

    def test_both_windows(self):
        s = self.build({"five_hour": {"used_percentage": 23.5, "resets_at": NOW + 600},
                        "seven_day": {"used_percentage": 41.2, "resets_at": NOW + 9000}},
                       {"a": NOW - 5, "old": NOW - 7 * 3600})
        self.assertEqual((s.remaining_percent, s.limit_window_minutes, s.quota_reset_seconds),
                         (76, 300, 600))
        self.assertEqual((s.secondary_available, s.secondary_remaining_percent,
                          s.secondary_limit_window_minutes, s.secondary_quota_reset_seconds),
                         (True, 58, 10080, 9000))
        self.assertEqual((s.tokens_today, s.tokens_7d, s.active_threads), (5, 9, 1))
        self.assertFalse(s.tokens_today_estimated or s.paid_credits_available)
        self.assertLessEqual(len(encode_snapshot(s, 1)), 180)

    def test_unavailable_and_expired(self):
        s = self.build(None)
        self.assertEqual((s.remaining_percent, s.limit_window_minutes, s.quota_reset_seconds,
                          s.secondary_available), (0, 0, 0, False))
        s = self.build({"five_hour": {"used_percentage": 99, "resets_at": NOW - 1},
                        "seven_day": {"used_percentage": 99, "resets_at": NOW}})
        self.assertEqual((s.remaining_percent, s.limit_window_minutes, s.quota_reset_seconds),
                         (100, 300, 0))
        self.assertEqual((s.secondary_remaining_percent, s.secondary_quota_reset_seconds), (100, 0))
        s = self.build({"five_hour": {"used_percentage": 150, "resets_at": NOW + 1}})
        self.assertEqual(s.remaining_percent, 0)
        encode_snapshot(s, 1)

    def test_sessions_expiry_and_cap(self):
        path = self.root / "s.json"
        sessions = {str(i): NOW for i in range(300)}
        sessions["x"] = NOW - 6 * 3600
        path.write_text(json.dumps({"sessions": sessions}))
        self.assertEqual(running_sessions(path, NOW + 1), 255)
        self.assertEqual(running_sessions(self.root / "missing.json", NOW), 0)

    def test_newer_detail_replaces_both_windows_and_preserves_json_format(self):
        rate = {"captured_at": NOW - 100,
                "five_hour": {"used_percentage": 60, "resets_at": NOW + 100},
                "seven_day": {"used_percentage": 80, "resets_at": NOW + 200}}
        newer = {"last_ok": {"captured_at": NOW - 50, "body": {
            "five_hour": {"utilization": 6, "resets_at": "2027-01-15T08:10:00.846431+00:00"},
            "seven_day": {"utilization": 17, "resets_at": "2027-01-15T10:30:00.846448+00:00"}}}}
        result = self.build(rate, detail=newer)
        self.assertEqual((result.remaining_percent, result.quota_reset_seconds,
                          result.secondary_remaining_percent, result.secondary_quota_reset_seconds),
                         (94, 600, 83, 9000))
        payload = json.loads(encode_snapshot(result, 1))
        self.assertEqual((payload["r"], payload["R"]), (94, 83))
        self.assertEqual(set(payload), {"v", "s", "t", "o", "r", "u", "q", "d", "n", "R",
                                        "U", "Q", "e", "w", "p", "b", "c", "x", "a"})
        for captured in (NOW - 100, NOW - 101, 1 << 64):
            newer["last_ok"]["captured_at"] = captured
            result = self.build(rate, detail=newer)
            self.assertEqual((result.remaining_percent, result.secondary_remaining_percent), (40, 20))

    def test_newer_detail_missing_or_invalid_window_is_unavailable(self):
        newer = {"last_ok": {"captured_at": NOW, "body": {
            "five_hour": {"utilization": 6, "resets_at": "not-a-date"}}}}
        result = self.build({"captured_at": NOW - 1}, detail=newer)
        self.assertEqual((result.limit_window_minutes, result.secondary_available), (0, False))


class ScannerTests(unittest.TestCase):
    def test_dedupe_buckets_window_and_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            projects = Path(tmp) / "projects"
            (projects / "p" / "sub").mkdir(parents=True)
            now = time.time()
            day = 86400
            lines = [record(now, "m1", "r1"), record(now, "m1", "r1"),   # duplicate block
                     record(now, "m2", "r2", (10, 0, 0, 0)),
                     record(now - 3 * day, "m3", "r3", (100, 0, 0, 0)),
                     record(now - 7 * day, "m4", "r4", (1000, 0, 0, 0)),  # outside 7 days
                     "not json {", '{"type":"assistant"}']
            (projects / "p" / "a.jsonl").write_text("\n".join(lines))
            (projects / "p" / "sub" / "b.jsonl").write_text(record(now, "m1", "r1") + "\n")
            old = projects / "p" / "old.jsonl"
            old.write_text(record(now, "m9", "r9", (7, 0, 0, 0)))
            os.utime(old, (now - 9 * day, now - 9 * day))
            scanner = TokenScanner(projects)
            self.assertEqual(scanner.totals(now), (20, 120))
            cached = dict(scanner.files)
            self.assertEqual(scanner.totals(now), (20, 120))
            self.assertTrue(all(scanner.files[k] is cached[k] for k in cached))  # not reparsed
            with open(projects / "p" / "a.jsonl", "a") as handle:
                handle.write("\n" + record(now, "m5", "r5", (1, 0, 0, 0)))
            self.assertEqual(scanner.totals(now), (21, 121))

    def test_cache_sequence_and_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = ClaudeSnapshotCache(60, root, root / "r.json", root / "s.json", root / "d.json")
            cache.snapshot()
            first = cache.sequence
            cache.snapshot()
            self.assertEqual(cache.sequence, first)
            (root / "s.json").write_text(json.dumps({"sessions": {"a": time.time()}}))
            self.assertEqual(cache.snapshot().active_threads, 1)
            self.assertEqual(cache.sequence, first + 1)


if __name__ == "__main__":
    unittest.main()
