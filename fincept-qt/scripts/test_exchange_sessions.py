#!/usr/bin/env python3
"""Unit tests for scripts/exchange_sessions.py.

Run directly: `python3 test_exchange_sessions.py` from the scripts/ dir.
Returns non-zero on any failure. No pytest dependency — uses unittest
to match the rest of the scripts/ test files (e.g. agents/.../tests/*.py).

Covers the math that's most likely to silently regress as the futures
product registry grows:
  - is_non_equity / _is_crypto_spot detection boundaries
  - session_end_local per instrument class
  - previous_session_end_utc across:
      * today's session already settled
      * today's session not yet at settle (use yesterday's)
      * weekend skip (Monday morning → Friday)
      * US holiday skip (Tuesday after Memorial Day → Friday)
      * crypto-spot uses 00:00 UTC and ignores weekends/holidays
      * FX uses NY 17:00 ET
"""

from __future__ import annotations

import os
import sys
import unittest
from datetime import datetime
from zoneinfo import ZoneInfo

# Ensure imports work whether invoked from scripts/ or repo root.
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from exchange_sessions import (  # noqa: E402
    _is_crypto_spot,
    is_non_equity,
    previous_session_end_utc,
    session_end_local,
)


UTC = ZoneInfo("UTC")
ET = ZoneInfo("America/New_York")


def _et(y, m, d, hh, mm=0):
    """Helper: build a tz-aware ET datetime, return its UTC equivalent."""
    return datetime(y, m, d, hh, mm, tzinfo=ET).astimezone(UTC)


class TestCryptoDetection(unittest.TestCase):
    def test_legit_crypto_pairs(self):
        for s in ("BTC-USD", "ETH-USD", "DOGE-USD", "SOL-USDT", "USDC-USD"):
            self.assertTrue(_is_crypto_spot(s), msg=s)
            self.assertTrue(is_non_equity(s), msg=s)

    def test_non_crypto_with_usd_substring_not_matched(self):
        # Tighter than the original `"-USD" in symbol` check.
        for s in ("WEIRD-USD-FOO", "MY-USD-X", "PRE-USD-POST"):
            self.assertFalse(_is_crypto_spot(s), msg=s)
            # is_non_equity also rejects these (no =F or =X suffix either)
            self.assertFalse(is_non_equity(s), msg=s)

    def test_equities_are_equities(self):
        for s in ("AAPL", "TSLA", "SPY", "QQQ", "BRK-B"):
            self.assertFalse(_is_crypto_spot(s), msg=s)
            self.assertFalse(is_non_equity(s), msg=s)


class TestSessionEndLocal(unittest.TestCase):
    def test_equity_default(self):
        self.assertEqual(session_end_local("AAPL"), (16, 0, "America/New_York"))

    def test_known_futures_products(self):
        self.assertEqual(session_end_local("ES=F"),  (16, 0, "America/New_York"))
        self.assertEqual(session_end_local("CL=F"),  (14, 30, "America/New_York"))
        self.assertEqual(session_end_local("GC=F"),  (13, 30, "America/New_York"))
        self.assertEqual(session_end_local("ZN=F"),  (15, 0, "America/New_York"))
        self.assertEqual(session_end_local("ZC=F"),  (14, 20, "America/New_York"))

    def test_unknown_futures_falls_back_to_equity_close(self):
        self.assertEqual(session_end_local("ZZZZ=F"), (16, 0, "America/New_York"))

    def test_fx(self):
        self.assertEqual(session_end_local("EUR=X"), (17, 0, "America/New_York"))

    def test_crypto(self):
        self.assertEqual(session_end_local("BTC-USD"), (0, 0, "UTC"))


class TestPreviousSessionEnd(unittest.TestCase):
    def test_es_after_todays_settle(self):
        # Wednesday 22:14 ET → Wed 16:00 ET (today's settle)
        now = _et(2026, 5, 13, 22, 14)
        got = previous_session_end_utc("ES=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 13, 16, 0, tzinfo=ET))

    def test_es_before_todays_settle(self):
        # Wednesday 12:00 ET → Tue 16:00 ET (today's hasn't happened yet)
        now = _et(2026, 5, 13, 12, 0)
        got = previous_session_end_utc("ES=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 12, 16, 0, tzinfo=ET))

    def test_monday_morning_skips_weekend(self):
        # Monday 10:00 ET → Friday 16:00 ET (weekend walk-back)
        now = _et(2026, 5, 11, 10, 0)
        got = previous_session_end_utc("ES=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 8, 16, 0, tzinfo=ET))

    def test_tuesday_after_memorial_day_skips_holiday(self):
        # Memorial Day 2026 = Mon May 25. Tuesday May 26 at 10:00 ET
        # → Friday May 22 16:00 ET (skip Mon holiday + weekend).
        now = _et(2026, 5, 26, 10, 0)
        got = previous_session_end_utc("ES=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 22, 16, 0, tzinfo=ET))

    def test_crude_uses_1430_et_settle(self):
        # Wed 22:00 ET → Wed 14:30 ET (CL's own settle, not equity's 16:00).
        now = _et(2026, 5, 13, 22, 0)
        got = previous_session_end_utc("CL=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 13, 14, 30, tzinfo=ET))

    def test_crude_before_its_settle_walks_to_yesterday(self):
        # Wed 14:00 ET (before CL's 14:30 settle) → Tue 14:30 ET.
        now = _et(2026, 5, 13, 14, 0)
        got = previous_session_end_utc("CL=F", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 12, 14, 30, tzinfo=ET))

    def test_crypto_uses_utc_midnight_ignores_weekends(self):
        # Saturday 12:00 UTC → Saturday 00:00 UTC (crypto trades 24/7,
        # no weekend skip).
        now = datetime(2026, 5, 9, 12, 0, tzinfo=UTC)  # Saturday
        got = previous_session_end_utc("BTC-USD", now)
        self.assertEqual(got, datetime(2026, 5, 9, 0, 0, tzinfo=UTC))

    def test_fx_uses_1700_et(self):
        # Wed 19:00 ET → Wed 17:00 ET
        now = _et(2026, 5, 13, 19, 0)
        got = previous_session_end_utc("EUR=X", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 13, 17, 0, tzinfo=ET))

    def test_fx_before_1700_walks_yesterday(self):
        # Wed 16:30 ET → Tue 17:00 ET
        now = _et(2026, 5, 13, 16, 30)
        got = previous_session_end_utc("EUR=X", now)
        self.assertEqual(got.astimezone(ET), datetime(2026, 5, 12, 17, 0, tzinfo=ET))


if __name__ == "__main__":
    unittest.main(verbosity=2)
