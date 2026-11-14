#!/usr/bin/env python3
"""Day-change reference for futures / FX quotes (exchange_sessions).

The batch quote prices a future's or FX pair's day change against the 1m bar
at a session end. It used the most recent session end at or before now, so
from a settle until the next reopen — and all weekend — a future was priced
against the settle of the session still on screen: Saturday 2026-11-14 showed
ES=F +8.00 (+0.11%) while it was down 32.25 (-0.42%) on Thursday's settle.

Pinned here: the reference is the settle BEFORE the most recent reopen
(Globex 18:00 ET, FX 17:00 ET, Sun-Thu), and Columbus / Veterans Day are not
market holidays (they were walked past as if NYSE/CME were shut).
"""

import os
import sys
from datetime import datetime
from zoneinfo import ZoneInfo

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas  # noqa: F401  (the holiday calendar needs it)
except ImportError:
    sys.exit(77)

import exchange_sessions as xs  # noqa: E402

ET = ZoneInfo("America/New_York")
UTC = ZoneInfo("UTC")
FAILURES = []


def et(y, m, d, hh, mm=0):
    return datetime(y, m, d, hh, mm, tzinfo=ET)


def check(name, sym, now, want):
    got = xs.day_change_reference_utc(sym, now.astimezone(UTC)).astimezone(ET)
    ok = got == want
    print(("PASS : " if ok else "FAIL : ") + name + ("" if ok else f" got {got} want {want}"))
    if not ok:
        FAILURES.append(name)


def main():
    # Fri 13 Nov 2026 was a Friday; Thu 12 the previous session.
    check("Saturday: ES vs Thursday's settle", "ES=F", et(2026, 11, 14, 0, 19), et(2026, 11, 12, 16))
    check("Sunday before reopen: still Friday's session", "ES=F", et(2026, 11, 15, 17, 0), et(2026, 11, 12, 16))
    check("Sunday after reopen: vs Friday's settle", "ES=F", et(2026, 11, 15, 19, 0), et(2026, 11, 13, 16))
    check("after settle, before reopen: vs the day before", "ES=F", et(2026, 5, 13, 16, 30), et(2026, 5, 12, 16))
    check("evening session: vs today's settle", "ES=F", et(2026, 5, 13, 22, 14), et(2026, 5, 13, 16))
    check("midday: vs yesterday's settle", "ES=F", et(2026, 5, 13, 12, 0), et(2026, 5, 12, 16))
    check("Monday morning: vs Friday", "ES=F", et(2026, 5, 11, 10, 0), et(2026, 5, 8, 16))
    check("crude Saturday: vs Thursday 14:30", "CL=F", et(2026, 11, 14, 9, 0), et(2026, 11, 12, 14, 30))
    check("crude evening: vs today's 14:30", "CL=F", et(2026, 5, 13, 22, 0), et(2026, 5, 13, 14, 30))
    check("FX evening: vs today's 17:00", "EURUSD=X", et(2026, 5, 13, 19, 0), et(2026, 5, 13, 17))
    check("FX afternoon: vs yesterday's 17:00", "EURUSD=X", et(2026, 5, 13, 16, 30), et(2026, 5, 12, 17))
    check("FX Saturday: vs Thursday's 17:00", "EURUSD=X", et(2026, 11, 14, 12, 0), et(2026, 11, 12, 17))
    # Veterans Day (Wed 11 Nov 2026): NYSE and CME were open.
    check("Veterans Day is a trading day", "ES=F", et(2026, 11, 12, 10, 0), et(2026, 11, 11, 16))
    # Columbus Day (Mon 12 Oct 2026) likewise.
    check("Columbus Day is a trading day", "ES=F", et(2026, 10, 13, 10, 0), et(2026, 10, 12, 16))
    # No Globex reopen on the eve of a full closure: Thu 24 Dec 2026's evening
    # would trade Christmas Day, so Saturday's session is Thursday's, priced
    # against Wednesday's settle.
    check("Christmas weekend: vs Wednesday's settle", "ES=F", et(2026, 12, 26, 12, 0), et(2026, 12, 23, 16))
    # Good Friday 26 Mar 2027: Thursday evening does not reopen either.
    check("Good Friday: vs Wednesday's settle", "ES=F", et(2027, 3, 26, 12, 0), et(2027, 3, 24, 16))
    # Real closures still walk back: Memorial Day Mon 25 May 2026.
    check("Memorial Day still skipped", "ES=F", et(2026, 5, 26, 10, 0), et(2026, 5, 22, 16))
    # Equities / crypto keep previous_session_end_utc.
    now = et(2026, 11, 14, 12, 0).astimezone(UTC)
    ok = xs.day_change_reference_utc("AAPL", now) == xs.previous_session_end_utc("AAPL", now)
    print(("PASS : " if ok else "FAIL : ") + "equities unchanged")
    if not ok:
        FAILURES.append("equities unchanged")

    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
