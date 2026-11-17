#!/usr/bin/env python3
"""The ER overview's after-hours / pre-market summary (scripts/extended_sessions.py).

What each case guards:

  - Each session is measured against ITS OWN close: after-hours against that
    day's, pre-market against the day before. Nasdaq's feed measures an
    after-hours print against the previous day's close — "-8%" all evening
    beside a stock that has moved +0.4% since the bell.
  - No matching close means no percentage, never another day's close.
  - Daily bar labels are dates and are read as written, whatever zone they
    are stamped in.
  - A stray print (a late-reported trade at another moment's price) is not
    the session's high, low or last.
  - Of two readings of one session's range the tighter bound wins, and only
    when the second reading is of the same day.
  - Nasdaq's volume is attached only to the session it belongs to.
  - Only plain US tickers are asked about at all.

Self-contained: no network, no pytest, a fixed clock.
"""

import os
import sys
from datetime import date, datetime

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas as pd
except ImportError:
    print("SKIP : pandas not available")
    sys.exit(77)

import extended_sessions as es

ET = es.ET
failures = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        failures.append(name)


def minute_bars(spec):
    """spec: [(date, 'HH:MM', n_minutes, start_price, step)] → 1m frame in UTC."""
    rows, idx = [], []
    for d, hm, n, p0, step in spec:
        h, m = map(int, hm.split(":"))
        t0 = pd.Timestamp(datetime(d.year, d.month, d.day, h, m, tzinfo=ET))
        for i in range(n):
            p = p0 + step * i
            idx.append((t0 + pd.Timedelta(minutes=i)).tz_convert("UTC"))
            rows.append({"Open": p, "High": p + 0.02, "Low": p - 0.02, "Close": p, "Volume": 0})
    return pd.DataFrame(rows, index=pd.DatetimeIndex(idx))


def daily_bars(rows, tz=None):
    idx = pd.DatetimeIndex([pd.Timestamp(d) for d, _, _ in rows])
    if tz:
        idx = idx.tz_localize(tz)
    return pd.DataFrame({"Close": [c for _, c, _ in rows], "Volume": [v for _, _, v in rows]}, index=idx)


FRI, MON, TUE = date(2026, 11, 13), date(2026, 11, 16), date(2026, 11, 17)
# Twenty earlier sessions so the average has something to stand on.
EARLIER = [(date(2026, 10, d), 30.0, 1_000_000.0) for d in range(5, 31) if date(2026, 10, d).weekday() < 5]
DAILY = daily_bars(EARLIER + [(FRI, 28.88, 2_000_000.0), (MON, 26.45, 4_000_000.0)])

BARS = minute_bars([
    (FRI, "16:00", 30, 28.80, -0.005),   # Friday after-hours
    (MON, "04:00", 60, 28.50, -0.01),    # Monday pre-market: 28.50 → 27.91
    (MON, "09:30", 30, 27.80, -0.04),    # regular session (ignored)
    (MON, "16:00", 60, 26.45, 0.002),    # Monday after-hours: 26.45 → 26.568
])

# ── pairing: each session against its own close ──────────────────────────────
now = datetime(2026, 11, 16, 23, 55, tzinfo=ET)
after, pre = es.summarise(BARS, DAILY, now)
check("after-hours is Monday's", after and after["date"] == MON.isoformat())
check("after-hours measured against Monday's close",
      after and after["ref_date"] == MON.isoformat() and abs(after["ref_close"] - 26.45) < 1e-9)
check("after-hours percentage is the move since the bell, not since Friday",
      after and abs(after["pct"] - (26.568 - 26.45) / 26.45 * 100) < 1e-6, str(after and after["pct"]))
check("pre-market is Monday's", pre and pre["date"] == MON.isoformat())
check("pre-market measured against Friday's close",
      pre and pre["ref_date"] == FRI.isoformat() and abs(pre["ref_close"] - 28.88) < 1e-9)
check("pre-market percentage", pre and abs(pre["pct"] - (27.91 - 28.88) / 28.88 * 100) < 1e-6)
check("neither session is live at 23:55", after and pre and not after["live"] and not pre["live"])
check("minutes counted", after and after["minutes"] == 60 and pre["minutes"] == 60)
check("last print is an instant in the session",
      after and datetime.fromtimestamp(after["last_ts"], ET).strftime("%Y-%m-%d %H:%M") == "2026-11-16 16:59")

# The yardstick is the 20 full days BEFORE the session, not including it.
vols = [v for d, _, v in EARLIER + [(FRI, 0, 2_000_000.0)] if d < MON][-20:]
check("average volume: the 20 sessions before, the session's own day excluded",
      after and abs(after["avg_volume"] - sum(vols) / len(vols)) < 1e-6)

# ── live flags follow the clock ───────────────────────────────────────────────
a2, p2 = es.summarise(BARS, DAILY, datetime(2026, 11, 16, 17, 0, tzinfo=ET))
check("after-hours live at 17:00 on its own day", a2 and a2["live"] and not p2["live"])
a3, p3 = es.summarise(BARS, DAILY, datetime(2026, 11, 17, 5, 0, tzinfo=ET))
check("yesterday's sessions are not live the next morning", a3 and not a3["live"] and not p3["live"])

# ── no matching close → no percentage ────────────────────────────────────────
lagging = daily_bars(EARLIER + [(FRI, 28.88, 2_000_000.0)])   # Monday's daily bar not there yet
a4, p4 = es.summarise(BARS, lagging, now)
check("after-hours with no close for its own day carries no percentage",
      a4 and a4["pct"] is None and a4["ref_close"] is None and a4["last"] is not None)
check("pre-market is unaffected by that", p4 and p4["pct"] is not None)
a5, p5 = es.summarise(BARS, None, now)
check("no daily frame: prices stand, percentages do not",
      a5 and p5 and a5["pct"] is None and p5["pct"] is None and a5["avg_volume"] is None)

# Tuesday pre-market while the daily frame still ends on Friday: Friday's
# close is not "the previous close", and the percentage would carry Monday.
tue = minute_bars([(MON, "09:30", 30, 27.80, -0.04), (MON, "16:00", 30, 26.45, 0.0), (TUE, "04:00", 20, 26.30, 0.0)])
_, p9 = es.summarise(tue, lagging, datetime(2026, 11, 17, 4, 30, tzinfo=ET))
check("pre-market with a daily frame a day behind carries no percentage",
      p9 and p9["date"] == TUE.isoformat() and p9["pct"] is None, str(p9 and (p9["ref_date"], p9["pct"])))
_, p10 = es.summarise(tue, DAILY, datetime(2026, 11, 17, 4, 30, tzinfo=ET))
check("pre-market against Monday's close once the daily frame has it",
      p10 and p10["ref_date"] == MON.isoformat() and p10["live"], str(p10 and p10["ref_date"]))

# ── early closes: after-hours runs 13:00-17:00 ───────────────────────────────
for d, want in [(date(2026, 11, 27), True), (date(2027, 11, 26), True), (date(2026, 11, 26), False),
                (date(2026, 11, 20), False), (date(2026, 12, 24), True), (date(2027, 12, 24), False),
                (date(2028, 7, 3), True), (date(2026, 7, 3), False), (MON, False)]:
    check(f"early close on {d}: {want}", es.is_early_close(d) is want)
BF = date(2026, 11, 27)
half = minute_bars([(BF, "09:30", 30, 50.0, 0.0), (BF, "13:00", 40, 50.0, 0.01)])
half_daily = daily_bars(EARLIER + [(date(2026, 11, 25), 49.0, 1e6), (BF, 50.0, 5e5)])
ah, _ = es.summarise(half, half_daily, datetime(2026, 11, 27, 13, 30, tzinfo=ET))
check("half day: trades after 13:00 are the after-hours session, and it is live",
      ah and ah["date"] == BF.isoformat() and ah["minutes"] == 40 and ah["live"], str(ah))
ah2, _ = es.summarise(half, half_daily, datetime(2026, 11, 27, 18, 0, tzinfo=ET))
check("half day: not live at 18:00", ah2 and not ah2["live"])
normal_mid, _ = es.summarise(BARS, DAILY, datetime(2026, 11, 16, 13, 30, tzinfo=ET))
check("normal day: 13:30 is not after-hours", normal_mid and not normal_mid["live"])

# ── daily labels are dates, in any zone ──────────────────────────────────────
for tz in ("UTC", "America/New_York", "Asia/Tokyo"):
    a6, p6 = es.summarise(BARS, daily_bars(EARLIER + [(FRI, 28.88, 2e6), (MON, 26.45, 4e6)], tz=tz), now)
    check(f"daily bars stamped in {tz} pair with the same days",
          a6 and p6 and a6["ref_date"] == MON.isoformat() and p6["ref_date"] == FRI.isoformat())

# ── stray prints ─────────────────────────────────────────────────────────────
stray = BARS.copy()
post = [i for i, t in enumerate(stray.index.tz_convert(ET)) if t.date() == MON and t.hour >= 16]
for col in ("Open", "High", "Low", "Close"):
    stray.iloc[post[20], stray.columns.get_loc(col)] = 28.88     # a cross at Friday's close
    stray.iloc[post[21], stray.columns.get_loc(col)] = 28.88     # ... two minutes running
    stray.iloc[post[-2], stray.columns.get_loc(col)] = 24.60     # a bad print just before the end
stray.iloc[post[30], stray.columns.get_loc("High")] = 31.00      # a stray high inside a sound minute
a7, _ = es.summarise(stray, DAILY, now)
check("a stray print is not the session's high", a7 and a7["high"] < 27.2, str(a7 and a7["high"]))
check("a stray print is not the session's low", a7 and a7["low"] > 26.0, str(a7 and a7["low"]))
check("the last price is the last bar's", a7 and abs(a7["last"] - 26.568) < 1e-6, str(a7 and a7["last"]))

# The move the panel exists for: it must show from its first minute, not
# after enough bars have piled up to outvote the old price.
for n_new in (1, 2, 3):
    jump = minute_bars([(MON, "16:00", 20, 26.45, 0.0), (MON, "16:20", n_new, 28.10, 0.0)])
    aj, _ = es.summarise(jump, DAILY, datetime(2026, 11, 16, 16, 25, tzinfo=ET))
    check(f"a 6% jump shows {n_new} minute(s) after it happens",
          aj and abs(aj["last"] - 28.10) < 1e-9 and aj["high"] > 28.0, str(aj and (aj["last"], aj["high"])))
ramp = minute_bars([(MON, "16:00", 10, 26.45, 0.0), (MON, "16:10", 12, 26.70, 0.27)])   # +1% a minute
ar, _ = es.summarise(ramp, DAILY, now)
check("a 1%-a-minute ramp is followed to its last bar", ar and abs(ar["last"] - (26.70 + 0.27 * 11)) < 1e-6,
      str(ar and ar["last"]))

# A real move is not a stray print: the neighbours move with it.
gap = minute_bars([(MON, "16:00", 30, 26.45, 0.0), (MON, "16:30", 30, 29.50, 0.01)])
a8, _ = es.summarise(gap, DAILY, now)
check("a sustained 11% move after the bell is kept", a8 and a8["last"] > 29.5 and a8["high"] > 29.5 and a8["low"] < 26.5)

# ── Nasdaq: parse, pair, tighten ─────────────────────────────────────────────
DOC = {"data": {
    "lastUpdateInfo": ["Data last updated Nov 16, 2026 08:00 PM ET.",
                       "This page will resume updating on Nov 17, 2026 04:00 PM ET."],
    "infoTable": {"rows": [{"consolidated": "$26.5693 -2.3107 (-8.00%)", "volume": "930,942",
                            "highPrice": "$26.88 (05:37:15 PM)", "lowPrice": "$26.45 (04:57:26 PM)"}]}}}
n = es.parse_nasdaq(DOC)
check("Nasdaq row parsed", n == {"date": MON, "volume": 930942.0, "high": 26.88, "low": 26.45}, str(n))
check("an empty answer is not a zero volume", es.parse_nasdaq({"data": {"infoTable": {"rows": []}}}) is None
      and es.parse_nasdaq({"data": None}) is None and es.parse_nasdaq(None) is None)
check("a row without a volume is no answer",
      es.parse_nasdaq({"data": {"infoTable": {"rows": [{"volume": "N/A"}]}}}) is None)

sess = {"date": MON.isoformat(), "last": 26.57, "high": 26.90, "low": 26.40, "volume": None}
check("volume attached to its own session", es.volume_for(sess, n) == 930942.0)
check("volume of another day is withheld", es.volume_for(dict(sess, date=TUE.isoformat()), n) is None)
undated = dict(n, date=None)
check("undated answer accepted when the ranges agree",
      es.volume_for(dict(sess, high=26.88, low=26.45), undated) == 930942.0)
check("undated answer withheld when the ranges do not", es.volume_for(dict(sess, high=28.50, low=27.73), undated) is None)
check("undated answer withheld when there is no range to compare",
      es.volume_for(dict(sess, high=None), undated) is None)

s1 = dict(sess, high=27.90, low=26.40)            # our high carries a stray print
es.apply_nasdaq(s1, dict(n, high=26.88, low=24.00))  # theirs carries a stray low
check("the tighter bound of each reading is kept", s1["high"] == 26.88 and s1["low"] == 26.40 and s1["volume"] == 930942.0,
      str(s1))
s2 = dict(sess)
es.apply_nasdaq(s2, dict(n, date=TUE))
check("another day's range and volume change nothing", s2["high"] == 26.90 and s2["low"] == 26.40 and s2["volume"] is None)
s3 = dict(sess, last=27.50, high=27.60)
es.apply_nasdaq(s3, n)
check("a range that would exclude the last price is not applied", s3["high"] == 27.60 and s3["low"] == 26.40)
s5 = dict(sess, high=27.90, low=26.40, live=True)
es.apply_nasdaq(s5, dict(n, high=26.88, low=26.45))
check("a live session's range is not narrowed by a summary that runs behind",
      s5["high"] == 27.90 and s5["low"] == 26.40 and s5["volume"] == 930942.0, str(s5))
s4 = dict(sess)
es.apply_nasdaq(s4, None)
check("no Nasdaq answer leaves the prices alone", s4["high"] == 26.90 and s4["volume"] is None)

# ── the volume is kept past midnight, per session ────────────────────────────
import tempfile
os.environ["FINTERM_DATA_DIR"] = tempfile.mkdtemp(prefix="extvol")
asks = []


def feed(answer):
    def ask(symbol, market):
        asks.append((symbol, market))
        return answer
    return ask


def sess_of(day=MON, live=False):
    return {"date": day.isoformat(), "live": live, "last": 26.57, "high": 26.90, "low": 26.40,
            "volume": None, "volume_partial": False}


# Read while it trades, then gone by the next look: a floor, marked as one.
s_live = sess_of(live=True)
es.add_volume("IONQ", "post", s_live, ask=feed(dict(n, volume=200000.0)), now=1000)
check("live: volume read", s_live["volume"] == 200000.0 and not s_live["volume_partial"])
s_next = sess_of()
es.add_volume("IONQ", "post", s_next, ask=feed(None), now=2000)
check("a mid-session reading is recalled as partial, not as the session's total",
      s_next["volume"] == 200000.0 and s_next["volume_partial"] is True, str(s_next))

# Read after the session ended: final, and never asked for again.
s_done = sess_of()
es.add_volume("AAPL", "post", s_done, ask=feed(dict(n)), now=1000)
check("finished: volume read and range tightened", s_done["volume"] == 930942.0 and s_done["high"] == 26.88)
asks.clear()
s_again = sess_of()
es.add_volume("AAPL", "post", s_again, ask=feed(None), now=1100)
check("a finished session with its total stored is not asked about again",
      asks == [] and s_again["volume"] == 930942.0 and not s_again["volume_partial"] and s_again["high"] == 26.88,
      f"asks={asks} {s_again}")
es.remember("AAPL", "post", MON.isoformat(), dict(n, volume=5.0), final=False)
check("a final reading is not replaced by a partial one", es.recall("AAPL", "post", MON.isoformat())["volume"] == 930942.0)

# Just ended: Nasdaq's summary still runs behind the tape, so this reading is
# not the total — stored as partial, and asked for again on the next look.
asks.clear()
s_early = sess_of()
es.add_volume("TSLA", "post", s_early, ask=feed(dict(n, volume=700000.0)), now=1000, settled=False)
check("a reading minutes after the close is kept as partial",
      s_early["volume"] == 700000.0 and es.recall("TSLA", "post", MON.isoformat())["final"] is False)
s_later = sess_of()
es.add_volume("TSLA", "post", s_later, ask=feed(dict(n, volume=930942.0)), now=1060, settled=True)
check("... and replaced by the settled total on the next look",
      len(asks) == 2 and s_later["volume"] == 930942.0 and es.recall("TSLA", "post", MON.isoformat())["final"] is True,
      f"asks={len(asks)} {s_later['volume']}")

# Finished, blanked, nothing stored: asked, then left alone for a while.
asks.clear()
for t_now in (1000, 1060, 1120):
    es.add_volume("MSFT", "pre", sess_of(), ask=feed(None), now=t_now)
check("a blanked session is asked about once, not every minute", len(asks) == 1, str(asks))
es.add_volume("MSFT", "pre", sess_of(), ask=feed(None), now=1000 + es._REASK_SECONDS + 1)
check("... and again after a while", len(asks) == 2, str(asks))
asks.clear()
for t_now in (1000, 1060):
    es.add_volume("MSFT", "post", sess_of(live=True), ask=feed(None), now=t_now)
check("a live session is asked about every time", len(asks) == 2, str(asks))

for label, args in [("another session's", ("IONQ", "pre", MON)), ("another day's", ("IONQ", "post", TUE)),
                    ("another symbol's", ("NVDA", "post", MON))]:
    check(f"{label} volume is not recalled for this one", es.recall(args[0], args[1], args[2].isoformat()) is None)

# ── which symbols ────────────────────────────────────────────────────────────
for sym, want in [("AAPL", True), ("SPY", True), ("BRK-B", True), ("F", True), ("BRK.B", False),
                  ("7203.T", False), ("VOD.L", False), ("SHOP.TO", False), ("^GSPC", False),
                  ("ES=F", False), ("EURUSD=X", False), ("BTC-USD", False), ("0700.HK", False), ("", False)]:
    check(f"extended session for {sym or '(empty)'}: {want}", es.has_us_extended_session(sym) is want)

check("empty frame yields no sessions", es.summarise(pd.DataFrame(), DAILY, now) == (None, None))

print()
if failures:
    print(f"{len(failures)} FAILED: " + ", ".join(failures))
    sys.exit(1)
print("all extended-session checks passed")
