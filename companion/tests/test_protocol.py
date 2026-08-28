import json
import unittest

from companion.codex_display.protocol import Snapshot, encode_snapshot


class ProtocolTests(unittest.TestCase):
    @staticmethod
    def snapshot(**changes):
        values = dict(generated_at=1784341234, utc_offset_minutes=480,
                      remaining_percent=68, limit_window_minutes=300,
                      quota_reset_seconds=201600, secondary_available=True,
                      secondary_remaining_percent=74,
                      secondary_limit_window_minutes=10080,
                      secondary_quota_reset_seconds=358400,
                      tokens_today=1250000, tokens_today_estimated=False,
                      tokens_7d=6840000, reset_credits=2,
                      next_credit_expiry_seconds=358400, active_threads=3)
        values.update(changes)
        return Snapshot(**values)

    def test_status_v2_is_compact_and_complete(self):
        data = encode_snapshot(self.snapshot(), 42)
        self.assertLessEqual(len(data), 180)
        self.assertEqual(set(json.loads(data)),
                         {"v", "s", "t", "o", "r", "u", "q", "n", "R", "U", "Q",
                          "d", "e", "w", "c", "x", "a"})

    def test_rejects_invalid_values(self):
        snapshot = self.snapshot(remaining_percent=101)
        with self.assertRaises(ValueError):
            encode_snapshot(snapshot, 1)
        with self.assertRaises(ValueError):
            encode_snapshot(self.snapshot(), 0)
        with self.assertRaises(ValueError):
            encode_snapshot(self.snapshot(generated_at=True), 1)
        with self.assertRaises(ValueError):
            encode_snapshot(self.snapshot(limit_window_minutes=0x100000000), 1)
        with self.assertRaises(ValueError):
            encode_snapshot(self.snapshot(secondary_remaining_percent=101), 1)
        with self.assertRaises(ValueError):
            encode_snapshot(self.snapshot(secondary_available=False), 1)


if __name__ == "__main__":
    unittest.main()
