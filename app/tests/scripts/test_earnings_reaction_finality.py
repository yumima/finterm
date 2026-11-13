#!/usr/bin/env python3
"""A print's reaction must not be settled from a session that is still open.

The bug this pins down: yfinance hands back a bar for the CURRENT session the
moment it opens, with a close that keeps moving all day. `_earnings_price_
reaction` read that as `p_after` and returned a finished "1-day earnings
reaction" built from a mid-morning quote.

Everything derived from it recomputes on the next load and self-corrects — the
typical move, the correlations, every predictor's walk-forward record. The
signal ledger does not: EarningsSignalRepository::resolve only ever touches
rows that are still unresolved, so whatever the tape said at 10:05 on the day
after a print became that print's recorded outcome permanently.

The fix splits the number: `reaction_pct` is populated only once the session
has closed, and until then the same figure is returned as `reaction_live_pct`,
which nothing scores, correlates or settles — the separation
`move_since_last_pct` already uses for the trailing row.

Self-contained: no network. The clock is frozen, because the whole point is
behaviour that differs between a session that is open and one that is not, and
a suite on the wall clock would only ever exercise whichever branch happened to
be live when it ran.
"""

import os
import sys
import traceback
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas as pd
except ImportError:
    # 77 = "the app venv isn't present" — skipped, not passed, so a missing
    # dependency can never mask a regression.
    print("SKIP : pandas not available")
    sys.exit(77)

for _name in ("yfinance", "numpy", "requests", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import yfinance_data as yd  # noqa: E402

ET = "America/New_York"
# A plain Thursday, and the Wednesday before it. Neither is a holiday.
PRINT_DAY = pd.Timestamp("2026-08-12", tz=ET)
NEXT_DAY = pd.Timestamp("2026-08-13", tz=ET)
FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


class _FrozenNow:
    """pd.Timestamp.now() pinned. _session_is_final reads it for the local
    clock test, and it is a staticmethod on the class, so patching the class
    attribute is what reaches it."""

    def __init__(self, ts):
        self.ts = ts

    def __call__(self, tz=None):
        if tz is None:
            return self.ts.tz_localize(None) if self.ts.tz is None else self.ts.tz_convert(None)
        return self.ts.tz_convert(tz)


def _freeze(ts):
    original = pd.Timestamp.now
    pd.Timestamp.now = staticmethod(_FrozenNow(ts))
    return original


def _hist(days, closes):
    """Daily bars stamped at exchange midnight, the shape yfinance returns."""
    idx = pd.DatetimeIndex([pd.Timestamp(d) for d in days]).tz_convert(ET)
    return pd.DataFrame({"Close": closes}, index=idx)


def _series(last_day):
    """Six sessions ending at `last_day`, so the 5-session run-up resolves."""
    days = [last_day - pd.Timedelta(days=k) for k in range(6, -1, -1)]
    return days, [90.0, 92.0, 94.0, 96.0, 98.0, 100.0, 110.0]


# ── the guard itself ─────────────────────────────────────────────────────────

def test_open_session_is_not_final():
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(NEXT_DAY.replace(hour=10, minute=5))
    try:
        check("today's bar mid-session is not final",
              yd._session_is_final(hist.index, len(hist) - 1) is False)
        check("an earlier bar is final even mid-session",
              yd._session_is_final(hist.index, len(hist) - 2) is True)
    finally:
        pd.Timestamp.now = original


def test_closed_session_is_final():
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(NEXT_DAY.replace(hour=16, minute=30))
    try:
        check("today's bar after the close is final",
              yd._session_is_final(hist.index, len(hist) - 1) is True)
    finally:
        pd.Timestamp.now = original


def test_the_settling_margin_after_the_bell():
    # In the first minutes after the bell the last daily close can still be the
    # 15:59 print rather than the closing auction. A ledger row is written once
    # and never revisited, so it must not be settled from the earlier of the
    # two.
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(NEXT_DAY.replace(hour=16, minute=5))
    try:
        check("not final five minutes after the bell",
              yd._session_is_final(hist.index, len(hist) - 1) is False)
    finally:
        pd.Timestamp.now = original


def test_a_non_us_bar_is_never_judged_by_the_us_clock():
    # 16:20 is the US close plus a margin, and it is the only close this
    # codebase knows. XETRA and Euronext close at 17:30 local, so judging a
    # Frankfurt bar by it would call the session final more than an hour
    # early — and this is the path that writes the ledger permanently. Non-US
    # listings must rest on the exact "a later bar exists" test alone.
    berlin_now = pd.Timestamp.now(tz="Europe/Berlin").normalize().replace(hour=16, minute=45)
    idx = pd.DatetimeIndex(
        [berlin_now.normalize() - pd.Timedelta(days=k) for k in range(4, -1, -1)]
    ).tz_convert("Europe/Berlin")
    original = _freeze(berlin_now)
    try:
        check("a Frankfurt bar is not final at 16:45 local, before its 17:30 close",
              yd._session_is_final(idx, len(idx) - 1) is False)
        check("…but a Frankfurt bar with a successor still is",
              yd._session_is_final(idx, len(idx) - 2) is True)
    finally:
        pd.Timestamp.now = original

    # A trailing bar that is merely STALE — thin trading, an old fetch — must
    # still settle, or a non-US print could sit unresolved forever.
    original = _freeze(berlin_now + pd.Timedelta(days=6))
    try:
        check("a week-old trailing Frankfurt bar is final, not live",
              yd._session_is_final(idx, len(idx) - 1) is True)
    finally:
        pd.Timestamp.now = original


def test_a_naive_stamp_is_never_judged_by_the_host_clock():
    # With no timezone on the stamp, any "now" we build is the HOST's clock,
    # which says nothing about the venue. A naive bar for a session still
    # trading can read as "tomorrow" to a host west of it, and calling that
    # final writes a running close into a record resolve() never revisits.
    naive = pd.DatetimeIndex([pd.Timestamp("2026-08-13") - pd.Timedelta(days=k)
                              for k in range(4, -1, -1)])
    original = _freeze(pd.Timestamp("2026-08-12 10:05", tz=ET))
    try:
        check("a naive trailing stamp is never called final",
              yd._session_is_final(naive, len(naive) - 1) is False)
        check("…but a naive stamp with a successor still is",
              yd._session_is_final(naive, len(naive) - 2) is True)
    finally:
        pd.Timestamp.now = original


def test_a_later_bar_settles_it_whatever_the_clock():
    # The exact test: if a LATER bar exists the earlier one is closed, and no
    # holiday, half-day or exchange calendar can argue with that.
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(NEXT_DAY.replace(hour=10, minute=5))
    try:
        check("a bar with a successor is final regardless of the hour",
              yd._session_is_final(hist.index, len(hist) - 3) is True)
    finally:
        pd.Timestamp.now = original


# ── end to end ───────────────────────────────────────────────────────────────

def _amc_print(now_ts):
    """An after-close print on PRINT_DAY, read at `now_ts`."""
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(now_ts)
    try:
        return yd._earnings_price_reaction(hist, PRINT_DAY.replace(hour=16))
    finally:
        pd.Timestamp.now = original


def test_live_reaction_is_withheld_from_the_scored_slot():
    reaction, runup, runup20, p_before, p_after, live = \
        _amc_print(NEXT_DAY.replace(hour=10, minute=5))
    check("reaction_pct is withheld while the session trades", reaction is None,
          f"got {reaction}")
    check("price_after is withheld too — it is not a settled close", p_after is None,
          f"got {p_after}")
    check("the number is still reported, in the live slot",
          live is not None and abs(live - 10.0) < 1e-9, f"got {live}")
    # 5 sessions back from the pre-print close: (100 - 90) / 90.
    check("the run-up is unaffected — it ends at a closed bar",
          runup is not None and abs(runup - 11.1111111111) < 1e-6, f"got {runup}")


def test_closed_reaction_populates_the_scored_slot():
    reaction, runup, runup20, p_before, p_after, live = \
        _amc_print(NEXT_DAY.replace(hour=16, minute=45))
    check("reaction_pct lands once the session closes",
          reaction is not None and abs(reaction - 10.0) < 1e-9, f"got {reaction}")
    check("price_after lands with it",
          p_after is not None and abs(p_after - 110.0) < 1e-9, f"got {p_after}")
    check("and the live slot is empty — never both", live is None, f"got {live}")


def test_settled_history_is_untouched():
    # A print old enough to have a successor bar must be unaffected by any of
    # this, at any hour. This is the regression that would hurt most: the fix
    # must not withhold a decade of settled reactions.
    days, closes = _series(NEXT_DAY)
    hist = _hist(days, closes)
    original = _freeze(NEXT_DAY.replace(hour=10, minute=5))
    try:
        old_print = (NEXT_DAY - pd.Timedelta(days=3)).replace(hour=16)
        reaction, _, _, p_before, p_after, live = \
            yd._earnings_price_reaction(hist, old_print)
        check("an older print still reports a settled reaction", reaction is not None,
              f"got {reaction}")
        check("with no live value beside it", live is None, f"got {live}")
        check("and a real price_after", p_after is not None)
    finally:
        pd.Timestamp.now = original


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all earnings-reaction finality tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
