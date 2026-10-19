import unittest
from unittest.mock import patch

from companion.codex_display.main import SnapshotCache
from companion.codex_display.metrics import Snapshot


class FakeClient:
    def __init__(self):
        self.fail = False

    async def request(self, method, params=None):
        if self.fail:
            raise RuntimeError("temporary")
        if method == "account/rateLimits/read":
            return {"rateLimits": {"primary": {"usedPercent": 25}}}
        if method == "account/usage/read":
            return {"dailyUsageBuckets": []}
        return {"data": []}


class CacheTests(unittest.IsolatedAsyncioTestCase):
    def test_official_zero_bucket_wins_over_local_estimate(self):
        cache = SnapshotCache(FakeClient(), 60)
        cache.snapshot = Snapshot(0, 0, 100, 0, 0, False, 0, 0, 0,
                                  0, False, 0, False, False, 0, 0, 0, 0)
        with patch("companion.codex_display.main.read_local_tokens", return_value=99):
            self.assertEqual(cache.current().tokens_today, 0)
            self.assertFalse(cache.current().tokens_today_estimated)

    async def test_refresh_failure_keeps_last_good_payload(self):
        client = FakeClient()
        cache = SnapshotCache(client, 0)
        with patch("companion.codex_display.main.read_local_tokens", return_value=None):
            first = await cache.encoded()
            client.fail = True
            second = await cache.encoded()
        self.assertEqual(first, second)

    async def test_startup_refresh_retries_until_network_is_up(self):
        client = FakeClient()
        client.fail = True
        cache = SnapshotCache(client, 60)
        sleeps = []

        async def fake_sleep(seconds):
            sleeps.append(seconds)
            if len(sleeps) == 2:
                client.fail = False

        with patch("companion.codex_display.main.asyncio.sleep", fake_sleep), \
                self.assertLogs(level="WARNING"):
            await cache.refresh_until_ready(30)
        self.assertEqual(sleeps, [30, 30])
        self.assertIsNotNone(cache.snapshot)
