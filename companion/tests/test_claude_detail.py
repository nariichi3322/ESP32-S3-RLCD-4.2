import copy
import unittest

from companion.codex_display.claude_detail import (
    decode_claude_detail, encode_claude_detail, parse_timestamp)

CAPTURED = 1794378572
NOW = 1794378600
FABLE_RESET = "2026-11-14T22:59:59.846567+00:00"
FULL = {
    "id": "full-reset-2611", "label": "One full limit reset (November 2026)",
    "resets_total": 1, "resets_left": 1, "starts_at": "2026-11-10T21:30:00+00:00",
    "ends_at": "2026-12-10T22:00:00+00:00",
    "clears": ["five_hour", "seven_day", "seven_day_overage_included"],
    "paused": False, "usable_now": True, "type": "full"}
FABLE = {
    "id": "launch-2610-fable",
    "label": "New model launch: one Fable reset for Max, Team Premium and Enterprise Premium",
    "resets_total": 1, "resets_left": 1, "starts_at": "2026-11-09T18:00:00+00:00",
    "ends_at": "2027-03-09T07:59:00+00:00",
    "clears": ["five_hour", "seven_day_overage_included"],
    "paused": False, "usable_now": True, "type": "full"}
BODY = {
    "five_hour": {"utilization": 6, "resets_at": "2026-11-11T10:29:59.846431+00:00"},
    "seven_day": {"utilization": 17, "resets_at": "2026-11-14T22:59:59.846448+00:00"},
    "seven_day_opus": None, "seven_day_sonnet": None,
    "cedar_ember": {"eligible": True, "grants": [FULL, FABLE]},
    "extra_usage": {"is_enabled": True, "monthly_limit": 4000, "used_credits": 786,
                    "utilization": 19.65, "currency": "USD", "decimal_places": 2},
    "limits": [
        {"kind": "session", "group": "session", "percent": 6,
         "resets_at": "2026-11-11T10:29:59.846431+00:00", "scope": None},
        {"kind": "weekly_all", "group": "weekly", "percent": 17,
         "resets_at": "2026-11-14T22:59:59.846448+00:00", "scope": None},
        {"kind": "weekly_scoped", "group": "weekly", "percent": 12,
         "resets_at": FABLE_RESET,
         "scope": {"model": {"id": None, "display_name": "Fable"}, "surface": None}}],
    "spend": {"used": {"amount_minor": 786, "currency": "USD", "exponent": 2},
              "limit": {"amount_minor": 4000, "currency": "USD", "exponent": 2},
              "percent": 20, "enabled": True}}
VECTORS = {
    "ok_full": "01013f024c0bf46a00000000680bf46a00000000b004efe7f86a00000000555344021203000000000000a00f00000000000001070201000000000000000100000000000000e0201b6b000000000205020100000000000000010000000000000044b18f6b00000000",
    "more_grants": "01017f024c0bf46a00000000680bf46a00000000b004efe7f86a00000000555344021203000000000000a00f00000000000001070201000000000000000100000000000000e0201b6b000000000205020100000000000000010000000000000044b18f6b00000000",
    "no_credential": "010200000000000000000000680bf46a0000000000000000000000000000000000ff00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
    "no_fable_spend_off": "010125004c0bf46a00000000680bf46a0000000000000000000000000000000000ff00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
    "eur": "01013f014c0bf46a00000000680bf46a000000001027efe7f86a000000004555520240e2010000000000000000000000000001070201000000000000000100000000000000e0201b6b00000000000000000000000000000000000000000000000000000000000000",
}


def detail(body=None):
    return {"last_ok": {"captured_at": CAPTURED, "body": copy.deepcopy(BODY if body is None else body)},
            "last_attempt": {"at": CAPTURED, "result": "ok"},
            "fetching_at": 0, "retry_until": 0}


def decoded(value):
    return decode_claude_detail(encode_claude_detail(value, NOW))


class ClaudeDetailTests(unittest.TestCase):
    def test_shared_vectors(self):
        full = detail()
        more = detail()
        more["last_ok"]["body"]["cedar_ember"]["grants"].append(
            {**FABLE, "ends_at": "2028-01-01T00:00:00Z"})
        no_credential = {"last_attempt": {"at": CAPTURED, "result": "no_credential"}}
        off = detail({"limits": [], "spend": {"enabled": False},
                      "cedar_ember": {"eligible": False, "grants": []}})
        eur = detail()
        body = eur["last_ok"]["body"]
        body["limits"][-1]["percent"] = 100
        body["cedar_ember"]["grants"] = [body["cedar_ember"]["grants"][0]]
        body["spend"] = {"enabled": True,
                         "used": {"amount_minor": 123456, "currency": "EUR", "exponent": 2},
                         "limit": {"amount_minor": 0, "currency": "EUR", "exponent": 2}}
        for name, value in (("ok_full", full), ("more_grants", more),
                            ("no_credential", no_credential),
                            ("no_fable_spend_off", off), ("eur", eur)):
            with self.subTest(vector=name):
                self.assertEqual(encode_claude_detail(value, NOW).hex(), VECTORS[name])

    def test_grant_filter_sort_and_classification(self):
        sample = detail()
        grants = sample["last_ok"]["body"]["cedar_ember"]["grants"]
        grants[:] = [
            {**FULL, "label": "partial", "clears": ["seven_day"],
             "ends_at": "2026-11-12T00:00:00Z", "usable_now": False},
            {**FABLE, "label": "fAbLe reset", "ends_at": "2026-11-14T00:00:00Z"},
            {**FULL, "ends_at": "2026-11-13T00:00:00Z"},
            {**FULL, "paused": True}, {**FULL, "resets_left": 0},
            {**FULL, "starts_at": "2026-11-12T00:00:00Z"},
            {**FULL, "ends_at": "2026-11-10T00:00:00Z"}]
        result = decoded(sample)
        self.assertTrue(result["flags"] & 64)
        self.assertEqual([g["type"] for g in result["grants"]], [1, 2])
        self.assertLess(result["grants"][0]["ends_at"], result["grants"][1]["ends_at"])
        grants[:] = [grants[0]]
        self.assertEqual(decoded(sample)["grants"][0]["type"], 3)
        grants[:] = [{**FULL, "clears": ["unknown_window"], "label": "other"}]
        self.assertEqual((decoded(sample)["grants"][0]["type"],
                          decoded(sample)["grants"][0]["clears"]), (0, 8))

    def test_each_ineligible_grant_is_filtered(self):
        for excluded in ({**FULL, "paused": True}, {**FULL, "resets_left": 0},
                         {**FULL, "starts_at": "2026-11-12T00:00:00Z"},
                         {**FULL, "ends_at": "2026-11-10T00:00:00Z"}):
            sample = detail()
            sample["last_ok"]["body"]["cedar_ember"]["grants"] = [excluded]
            result = decoded(sample)
            self.assertEqual(result["flags"] & 96, 32)
            self.assertEqual(result["grants"], [])

    def test_fable_match_is_case_insensitive_and_missing_is_known(self):
        sample = detail()
        sample["last_ok"]["body"]["limits"][-1]["scope"]["model"]["display_name"] = "fAbLe"
        self.assertEqual(decoded(sample)["flags"] & 3, 3)
        self.assertEqual(decoded(detail({"limits": []}))["flags"] & 3, 1)
        self.assertEqual(decoded(detail({}))["flags"] & 3, 0)
        self.assertEqual(decoded(detail({"limits": [None]}))["flags"] & 3, 0)
        self.assertEqual(decoded(detail({"limits": [{"kind": "weekly_scoped"}]}))["flags"] & 3, 0)
        self.assertEqual(parse_timestamp(FABLE_RESET), 1794697199)

    def test_invalid_values_clear_the_corresponding_group(self):
        for currency, exponent in ((None, 2), ("", 2), ("usd", 2), ("US1", 2), ("UŚD", 2), ("USD", 10),
                                   ("USD", -1), ("USD", True)):
            with self.subTest(currency=currency, exponent=exponent):
                sample = detail()
                sample["last_ok"]["body"]["spend"]["used"].update(
                    currency=currency, exponent=exponent)
                result = decoded(sample)
                self.assertEqual(result["flags"] & 28, 12)
                self.assertEqual((result["currency"], result["exponent"]), ("", 255))
        for amount in (-1, 1 << 64, 1.5, True):
            sample = detail()
            sample["last_ok"]["body"]["spend"]["used"]["amount_minor"] = amount
            self.assertFalse(decoded(sample)["flags"] & 16)
        for percent in (-1, 101, float("nan"), float("inf"), True):
            sample = detail()
            sample["last_ok"]["body"]["limits"][-1]["percent"] = percent
            self.assertEqual(decoded(sample)["flags"] & 3, 0)
        sample = detail()
        sample["last_ok"]["body"]["cedar_ember"]["grants"][0]["resets_total"] = 1 << 64
        result = decoded(sample)
        self.assertFalse(result["flags"] & 32)
        self.assertEqual(result["grants"], [])

    def test_extra_usage_fallback_and_spend_priority(self):
        sample = detail()
        del sample["last_ok"]["body"]["spend"]
        result = decoded(sample)
        self.assertEqual((result["currency"], result["exponent"], result["spend_used"],
                          result["spend_limit"]), ("USD", 2, 786, 4000))
        sample["last_ok"]["body"]["spend"] = {"enabled": False}
        self.assertEqual(decoded(sample)["flags"] & 28, 4)

    def test_source_state_uses_newest_attempt(self):
        sample = detail()
        for result, expected in (("ok", 1), ("no_credential", 2), ("auth_rejected", 3),
                                 ("rate_limited", 4), ("error", 5)):
            for at in (CAPTURED, CAPTURED + 1):
                sample["last_attempt"] = {"at": at, "result": result}
                self.assertEqual(decoded(sample)["source_state"], expected)
            sample["last_attempt"]["at"] = CAPTURED - 1
            self.assertEqual(decoded(sample)["source_state"], 1)
        self.assertEqual(decoded(None)["source_state"], 0)
        self.assertEqual(decoded({"last_attempt": {"at": NOW, "result": "error"}})["source_state"], 5)
        sample["last_attempt"] = {"at": NOW, "result": []}
        self.assertEqual(decoded(sample)["source_state"], 0)


if __name__ == "__main__":
    unittest.main()
