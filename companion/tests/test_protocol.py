import json
import unittest

from companion.codex_display.protocol import Snapshot, encode_snapshot


class ProtocolTests(unittest.TestCase):
    def test_status_v1_is_compact_and_complete(self):
        data = encode_snapshot(Snapshot(1784341234, 480, 68, 10080, 201600,
                                        1250000, False, 6840000, 2, 358400, 3), 42)
        self.assertLessEqual(len(data), 180)
        self.assertEqual(set(json.loads(data)),
                         {"v", "s", "t", "o", "r", "u", "q", "d", "e", "w", "c", "x", "a"})

    def test_rejects_invalid_values(self):
        snapshot = Snapshot(0, 0, 101, 0, 0, 0, False, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            encode_snapshot(snapshot, 1)
        with self.assertRaises(ValueError):
            encode_snapshot(Snapshot(0, 0, 0, 0, 0, 0, False, 0, 0, 0, 0), 0)
        with self.assertRaises(ValueError):
            encode_snapshot(Snapshot(True, 0, 0, 0, 0, 0, False, 0, 0, 0, 0), 1)
        with self.assertRaises(ValueError):
            encode_snapshot(Snapshot(0, 0, 0, 0x100000000, 0, 0,
                                     False, 0, 0, 0, 0), 1)


if __name__ == "__main__":
    unittest.main()
