#!/usr/bin/env python3
"""Weekly brief: the one-page issue's sections and its posts.

They go to the PUBLIC site, so beyond "is it right" the tests pin what must
never be written: the calendar feed's consensus figures, and any TradingView /
Yahoo number behind a screen's ranking (their terms forbid redistribution).
No network.
"""

import os
import sys
from datetime import date, timedelta

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import weekly_brief_core as wb  # noqa: E402

FAILURES = []


def ff(title, when, impact="High", country="USD", forecast="0.3%", previous="0.2%"):
    return {"title": title, "country": country, "date": when, "impact": impact,
            "forecast": forecast, "previous": previous}


# ── top-tier rule (mirrors MacroCalendar.h) ──────────────────────────────────

def test_kind_matches_the_app():
    assert wb.macro_kind("FOMC Member Waller Speaks") == "FED"
    assert wb.macro_kind("Unemployment Claims") == "LABOR"
    assert wb.macro_kind("Core PPI m/m") == "INFLATION"
    assert wb.macro_kind("ISM Services PMI") == "GROWTH"
    assert wb.macro_kind("Pending Home Sales m/m") == ""


def test_top_tier_us():
    assert wb.is_top_tier_us(ff("CPI m/m", "x"))
    assert not wb.is_top_tier_us(ff("CPI m/m", "x", country="EUR"))
    assert wb.is_top_tier_us(ff("FOMC Member Waller Speaks", "x", impact="Medium"))
    assert not wb.is_top_tier_us(ff("Pending Home Sales m/m", "x", impact="Medium"))
    assert not wb.is_top_tier_us(ff("Bank Holiday", "x", impact="Holiday"))
    assert not wb.is_top_tier_us(ff("Crude Oil Inventories", "x", impact="Low"))


def test_week_ahead_is_the_coming_trading_week():
    for run in (date(2026, 11, 14), date(2026, 11, 15)):  # Saturday, Sunday
        assert wb.week_ahead(run) == (date(2026, 11, 16), date(2026, 11, 20)), run
    # a weekday catch-up or rerun lists the rest of THIS week, not next week's
    assert wb.week_ahead(date(2026, 11, 16)) == (date(2026, 11, 16), date(2026, 11, 20))
    assert wb.week_ahead(date(2026, 11, 19)) == (date(2026, 11, 19), date(2026, 11, 20))


def test_issue_week_survives_a_monday_catch_up():
    sat, sun, mon, wed = (date(2026, 11, d) for d in (14, 15, 16, 18))
    # the Saturday issue already published (W46, filed under Mon Nov 9) keeps its key and URL
    assert wb.issue_key(sat) == "2026-W46" and wb.issue_day(sat) == date(2026, 11, 9)
    for run in (sun, mon, wed):  # Sunday run, or the timer catching up after the machine was off
        assert wb.issue_key(run) == "2026-W46" and wb.issue_day(run) == date(2026, 11, 9), run
    # the next Sunday is the next issue, not a replacement of the catch-up's
    assert wb.issue_key(date(2026, 11, 22)) == "2026-W47"


def test_events_grouped_per_instant_in_eastern_time():
    rows = [
        ff("CPI m/m", "2026-11-18T08:30:00-05:00"),
        ff("Core CPI m/m", "2026-11-18T08:30:00-05:00"),
        ff("CPI m/m", "2026-11-18T08:30:00-05:00"),  # thisweek + nextweek overlap
        ff("Unemployment Claims", "2026-11-19T13:30:00+00:00", impact="Medium"),  # UTC → 8:30 ET
        ff("FOMC Statement", "2026-11-14T14:00:00-05:00"),  # before the window
        ff("German ZEW", "2026-11-18T05:00:00-05:00", country="EUR"),
    ]
    ev = wb.macro_events(rows, date(2026, 11, 16), date(2026, 11, 20))
    assert len(ev) == 2, ev
    assert ev[0]["titles"] == ["CPI m/m", "Core CPI m/m"]
    assert ev[1]["when"].hour == 8 and ev[1]["when"].minute == 30, ev[1]


def test_week_ahead_never_prints_consensus():
    mon, fri = date(2026, 11, 16), date(2026, 11, 20)
    rows = [ff("CPI m/m", "2026-11-18T08:30:00-05:00", forecast="0.37%", previous="0.41%"),
            ff("CPI y/y", "2026-11-18T08:30:00-05:00"), ff("FOMC Meeting Minutes", "2026-11-18T14:00:00-05:00")]
    text = "\n".join(wb.week_ahead_section(wb.macro_events(rows, mon, fri), mon, fri))
    # a row per day; "CPI m/m" and "CPI y/y" are one release
    assert "| Wed 18 | 8:30 CPI · 14:00 FOMC Meeting Minutes |" in text, text
    assert "0.37" not in text and "0.41" not in text, text
    assert wb.week_ahead_section([], date(2026, 11, 27), date(2026, 11, 27))[0] == "## Week ahead, Nov 27"
    assert "Nov 30–Dec 4" in wb.week_ahead_section([], date(2026, 11, 30), date(2026, 12, 4))[0]


def test_macro_failure_is_said_not_an_empty_week():
    text = "\n".join(wb.week_ahead_section([], date(2026, 11, 16), date(2026, 11, 20), error="feed down"))
    assert "No release list this week: feed down" in text and "No top-tier" not in text


def test_coverage_unread_week_is_not_a_quiet_week():
    mon, fri = date(2026, 11, 16), date(2026, 11, 20)
    # Saturday: only this week's file (next week's is a 404) — the window is not covered
    this_week = [ff("German ZEW", "2026-11-10T05:00:00-05:00", country="EUR")]
    assert "not published yet" in wb.macro_coverage(this_week, mon, fri)
    refused = wb.macro_coverage(this_week, mon, fri, ["ff_calendar_nextweek.json: HTTP 429"])
    assert "could not be read" in refused and "HTTP 429" in refused and "not published" not in refused
    # any row on the week's days (any country) means the file was read: a quiet US week is real
    assert wb.macro_coverage(this_week + [ff("German ZEW", "2026-11-17T05:00:00-05:00", country="EUR")],
                             mon, fri) is None


# ── markets and economy ──────────────────────────────────────────────────────

def _days(n, end=date(2026, 11, 20)):
    return [end - timedelta(days=n - 1 - i) for i in range(n)]


def _series():
    d = _days(500)
    months = [date(2024 + (i // 12), i % 12 + 1, 1) for i in range(34)]  # Jan 2024 .. Oct 2026
    return {
        "DGS10": [(x, 4.0 + 0.001 * i) for i, x in enumerate(d)],
        "DGS2": [(x, 3.5) for x in d[:-1]],  # the last day is not quoted: the spread ends a day earlier
        "DFF": [(x, 3.88) for x in d], "DTWEXBGS": [(x, 120.0 + (1.2 if x > d[-4] else 0)) for x in d],
        "DCOILWTICO": [(x, 80.0) for x in d], "NFCI": [(x, -0.5) for x in d],
        "A191RL1Q225SBEA": [(date(2025, q, 1), v) for q, v in ((1, -0.5), (4, 2.2), (7, 4.3))],
        "CPIAUCNS": [(m, 300 * 1.002 ** i) for i, m in enumerate(months) if m != date(2025, 10, 1)],
        "PCEPILFE": [(m, 100 * 1.002 ** i) for i, m in enumerate(months)],
        "UNRATE": [(m, 4.1) for m in months], "PAYEMS": [(m, 150000 + 165 * i) for i, m in enumerate(months)],
        "ICSA": [(x, 189000.0) for x in d[::7]],
    }


def test_market_rows_latest_week_change_and_trend():
    rows = {r["name"]: r for r in wb.market_rows(_series())}
    r = rows["10-year Treasury"]
    assert r["value"] == "4.50%" and r["change"] == "+1 bp" and r["asof"] == "Nov 20", r
    assert len(r["trend"]) == 12 and r["trend"][0] == "▁" and r["trend"][-1] == "█", r
    assert rows["10-year minus 2-year"]["value"] == "+100 bp" and rows["10-year minus 2-year"]["asof"] == "Nov 19"
    assert rows["Broad dollar index"]["change"] == "+1.0%" and rows["Fed funds, effective"]["change"] == "0 bp"
    assert rows["Financial conditions, NFCI"]["value"] == "−0.50 loose", rows["Financial conditions, NFCI"]


def test_a_series_not_read_is_a_dash_never_a_zero():
    s = _series()
    del s["DGS2"], s["UNRATE"]
    m = {r["name"]: r for r in wb.market_rows(s)}
    assert m["2-year Treasury"]["value"] == "—" and m["10-year minus 2-year"]["value"] == "—"
    assert {r["name"]: r for r in wb.economy_rows(s)}["Unemployment rate"]["value"] == "—"
    assert "could not be read" in "\n".join(wb.economy_section({}))
    got = wb.fetch_macro_series(date(2026, 11, 22), get=lambda sid, start: (
        {"error": "RATE"} if sid == "DGS2" else {"observations": []} if sid == "DFF"
        else {"observations": [{"date": "2026-11-20", "value": 1.0}]}))
    assert "DGS2" not in got and "DFF" not in got and got["DGS10"] == [(date(2026, 11, 20), 1.0)], got

    def down(sid, start):
        raise OSError("down")
    assert wb.fetch_macro_series(date(2026, 11, 22), get=down) == {}


def test_economy_rows_are_between_the_months_themselves():
    e = {r["name"]: r for r in wb.economy_rows(_series())}
    # October 2025 was never published: October 2026 has no y/y, and says so
    assert e["CPI, y/y"]["value"] == "2.4%" and e["CPI, y/y"]["asof"] == "Sep (Oct: no base)", e["CPI, y/y"]
    assert e["Core PCE, y/y"]["asof"] == "Oct", e
    assert e["Payrolls, monthly change"]["value"] == "+165K", e
    assert e["Real GDP, q/q annualised"]["value"] == "4.3%" and e["Real GDP, q/q annualised"]["prior"] == "2.2%"
    assert e["Real GDP, q/q annualised"]["asof"] == "Q3 2025" and e["Initial jobless claims"]["value"] == "189K"
    # November 2026: its y/y is there, and October's is not the release before it
    s = _series()
    s["CPIAUCNS"].append((date(2026, 11, 1), 330.0))
    n = {r["name"]: r for r in wb.economy_rows(s)}["CPI, y/y"]
    assert n["asof"] == "Nov" and n["prior"] == "—", n
    gap = wb.economy_points([(date(2026, 1, 1), 100.0), (date(2026, 3, 1), 130.0)], "diff_k")
    assert gap == [], "two months apart is not a monthly change"


def test_spark():
    assert wb.spark([1, 2]) == "" and wb.spark([5, 5, 5]) == "▄▄▄" and wb.spark([0, 1, 2]) == "▁▅█"
    long = wb.spark(list(range(250)))
    assert len(long) == 12 and long[-1] == "█" and long[0] == "▁"


# ── at a glance ──────────────────────────────────────────────────────────────

EV = {"from": "1990", "all": {"weeks": 1904, "up": 0.728, "mean": 0.029, "dd10": 0.136, "week_move": 0.021},
      "states": {"bearish": {"weeks": 346, "up": 0.578, "mean": 0.007, "dd10": 0.321, "week_move": 0.0266}},
      "rule": {"cagr": 0.0982, "worst": -0.353}, "always": {"cagr": 0.1082, "worst": -0.547}}


def test_regime_states_are_the_tested_definition():
    f = wb.regime_state
    assert [f(101, 100, 19.9), f(101, 100, 20), f(101, 100, 45)] == ["bullish", "bullish_nervous", "bullish_nervous"]
    assert [f(100, 100, 15), f(99, 100, 29.9), f(99, 100, 30)] == ["bearish", "bearish", "fear"]
    assert set(wb.STANCE) == set(wb.LABELS) == set(wb.REGIMES)
    # the stance is the tested rule: full above the average, half below
    assert [wb.STANCE[k] for k in wb.REGIMES] == [1.0, 1.0, 0.5, 0.5]
    ev = wb.load_evidence()  # the shipped record holds every state the call can make
    assert set(ev["states"]) == set(wb.REGIMES) and all(ev["states"][k]["weeks"] > 50 for k in wb.REGIMES)
    assert ev["rule"]["worst"] > ev["always"]["worst"] and ev["rule"]["cagr"] < ev["always"]["cagr"]


def _regime_series(last, vix, n=260, end=date(2026, 11, 20)):
    d = _days(n, end)
    return {"SP500": [(x, 100.0) for x in d[:-1]] + [(d[-1], last)], "VIXCLS": [(x, vix) for x in d]}


def test_market_regime_needs_both_series_200_closes_and_this_week():
    today = date(2026, 11, 22)
    r = wb.market_regime(_regime_series(95.0, 25.0), today)
    assert r["state"] == "bearish" and r["date"] == date(2026, 11, 20) and abs(r["week"] + 0.05) < 1e-9, r
    assert wb.market_regime(_regime_series(95.0, 25.0), date(2026, 11, 25)) is None  # five days old
    assert wb.market_regime(_regime_series(95.0, 25.0, n=199), today) is None
    assert wb.market_regime({"SP500": _regime_series(95, 25)["SP500"]}, today) is None
    s = _regime_series(95.0, 25.0)
    s["VIXCLS"] = s["VIXCLS"][:-1]  # the VIX a day behind: the last day both hold
    assert wb.market_regime(s, today)["date"] == date(2026, 11, 19)
    # the average is the 200 closes up to and including the day
    assert abs(wb.market_regime(_regime_series(95.0, 25.0), today)["price_gap"] - (95 / 99.975 - 1)) < 1e-9


def test_the_call_says_the_state_the_stance_and_its_record():
    today = date(2026, 11, 22)
    r = wb.market_regime(_regime_series(95.0, 25.0), today)
    text = " ".join(wb.call_lines(r, EV))
    assert "**Bearish: half risk.** S&P 500 below its 200-day average, fear elevated, at the Nov 20 close." in text, text
    assert "32% of such weeks (all weeks: 14%)" in text and "346 such weeks" in text
    assert "58% of the time (all: 73%)" in text and "+0.7% (+2.9%)" in text and "weekly move 2.7%" in text, text
    assert "9.8% a year, worst fall 35%; always invested 10.8%, 55%" in text and "Borderline" not in text, text
    for vendor in ("95", "25.0", "99.9"):
        assert vendor not in text, vendor  # the state is published, never the index or the VIX
    border = lambda px, vix: " ".join(wb.call_lines(wb.market_regime(_regime_series(px, vix), today),
                                                    {**EV, "states": {k: EV["states"]["bearish"] for k in wb.REGIMES}}))
    assert "Borderline: within 1% of the average." in border(99.5, 19.5)  # 20 is not a line below the average
    assert "Borderline: the VIX a point from its line." in border(95.0, 29.5)
    assert "Borderline: the VIX a point from its line." in border(110.0, 20.5) and "Borderline" not in border(110.0, 29.5)
    assert "full risk" in border(110.0, 15.0) and "High fear: half risk" in border(95.0, 31.0)
    bare = " ".join(wb.call_lines(r, {}))
    assert "risk" not in bare and "Bearish" in bare  # no record, no stance


def test_glance_section():
    today = date(2026, 11, 22)
    ev = wb.macro_events([ff("Core PCE Price Index m/m", "2026-11-25T08:30:00-05:00"),
                          ff("Prelim GDP q/q", "2026-11-25T08:30:00-05:00"),
                          ff("Flash Services PMI", "2026-11-23T09:45:00-05:00"),
                          ff("FOMC Meeting Minutes", "2026-11-25T14:00:00-05:00")], date(2026, 11, 23), date(2026, 11, 27))
    s = {**_series(), **_regime_series(95.0, 25.0)}
    s["DCOILWTICO"] = [(x, 80.0) for x in _days(400, date(2026, 11, 12))]  # ten days old: not the past week
    text = "\n".join(wb.glance_section(s, {"up5_share": 0.48, "above200_share": 0.53, "sectors": list("ABCD")}, ev, None,
                                       today, EV))
    assert text.startswith("## At a glance") and "**Past week:** S&P 500 down to Nov 20; 48% of US stocks" in text, text
    assert "strongest A, weakest D" in text and "10-year +1 bp" in text and "oil" not in text, text
    # most market-moving kind first (Fed, inflation, labour, growth)
    assert "**Week ahead:** FOMC Meeting Minutes (Wed), Core PCE Price Index (Wed), Flash Services PMI (Mon) and 1 more." in text, text
    none = "\n".join(wb.glance_section({}, None, [], "down", today, EV))
    assert "No market call" in none and "Past week" not in none and "calendar was not available" in none, none


# ── screens, insiders, funds ─────────────────────────────────────────────────

def _doc():
    ev = {"tested": True, "excess": 0.0174, "t": 0.51}
    return {
        "asof": "2026-11-13",
        "strategies": [
            {"id": "trend_template", "name": "Minervini Trend Template", "symbols": ["AAA", "BBB"], "evidence": ev},
            {"id": "rs_leaders", "name": "RS Leaders (top 25)", "symbols": ["AAA", "BBB", "CCC", "EEE"], "evidence": ev},
            {"id": "momentum", "name": "Momentum 12-1", "symbols": ["AAA", "CCC"], "evidence": ev},
            {"id": "canslim", "name": "CAN SLIM (partial)", "symbols": [], "evidence": {"tested": False}},
            {"id": "luk_pullback", "name": "Leader Pullback", "symbols": ["D_D"],
             "evidence": {"tested": True, "avg_R": -0.17, "trades": 1529, "excess": -0.2, "t": -5.7}},
            {"id": "buffett", "name": "Buffett Value", "symbols": ["AAA"], "evidence": ev},
        ],
        "rows": {"AAA": {"name": "Alpha Corp", "price": 123.45, "rs": 99, "r12m": 0.8765, "mos": 0.4321}},
    }


def test_screens_part_names_records_no_vendor_numbers():
    text = "\n".join(wb.screens_part(_doc(), per_screen=3))
    assert "| RS Leaders | AAA, BBB, CCC | +1.7% (+0.5) |" in text, text
    assert "Trend Template" not in text  # folded into RS Leaders
    assert "| CAN SLIM | — | not tested |" in text and "| Leader Pullback | D\\_D | −0.17R |" in text, text
    assert "**On three or more screens:** AAA (3)." in text, text  # the template is not counted
    for vendor in ("123.45", "0.8765", "87.6", "43.2", "Alpha"):
        assert vendor not in text, vendor
    assert "not a recommendation" in text and "none beat it with statistical significance" in text
    assert "2026-11-13 close" in text
    # names two of the parts hold, counted by their screens; only from parts the issue holds
    x = "\n".join(wb.screens_part(_doc(), rows=[{"symbol": "CCC"}, {"symbol": ""}, {"symbol": "ZZZ"}],
                                  flows={"added": [{"ticker": "BBB"}]}))
    assert "**On a screen, insiders buying:** CCC (2)." in x and "**On a screen, funds adding:** BBB (1)." in x, x
    assert "insiders buying" not in text and "funds adding" not in text
    doc = _doc()
    doc.update(intraday=True, stale_prices=2)
    doc["strategies"][1]["evidence"] = {"tested": True, "excess": 0.05, "t": 2.4}
    text = "\n".join(wb.screens_part(doc))
    assert "none beat" not in text and "before its close" in text and "2 stocks ranked on an earlier close" in text, text


def test_screen_failure_is_said():
    text = "\n".join(wb.screens_part(None, "the scan timed out"))
    assert "could not be run" in text and "timed out" in text


ROWS = [{"symbol": f"S{i}", "issuer": f"Co_{i}", "value": 1e6 - i, "insiders": 2, "avg_price": 1.0,
         "first_trade": "2026-11-16", "last_trade": "2026-11-16", "roles": ["CEO"]} for i in range(12)]


def test_insider_part_is_short_and_keeps_the_base_rate():
    text = "\n".join(wb.insider_part(ROWS, ["3 of 5 days were read"], 5))
    assert "| S4 |" in text and "| S5 |" not in text and "Co\\_0" in text, text
    assert "12 companies" in text and "$50K or more" in text and "/insider-cluster-buys/" in text
    assert "1.8%" in text and "1.2%" in text and "trailed it by 0.2% over three" in text, text
    assert "> 3 of 5 days were read" in text
    assert "no company had two or more insiders" in "\n".join(wb.insider_part([], []))


FLOWS = {"quarter": "2026-09-30", "filed_to": "2026-11-24",
         "added": [{"ticker": "INTC", "net": 9.54e9, "adds": 81, "trims": 35}],
         "trimmed": [{"ticker": "PM", "net": -7.67e9, "adds": 10, "trims": 20},
                     {"ticker": "MU", "net": -7.3e9, "adds": 88, "trims": 74}]}


def test_funds_part():
    text = "\n".join(wb.funds_part(FLOWS, 5))
    assert "| INTC | +$9.5B | 81 / 35 | PM | −$7.7B | 10 / 20 |" in text, text
    assert "|  |  |  | MU | −$7.3B | 88 / 74 |" in text, text  # sides of different length
    assert "Sep 30, 2026, filed to Nov 24" in text
    assert "13F index has not been read" in "\n".join(wb.funds_part({"error": "the 13F index has not been read yet"}))


def test_stock_flows_sums_by_stock_and_leaves_out_what_is_not_a_trade():
    import tempfile
    import sec_13f_bulk as b
    tmp = tempfile.mkdtemp(prefix="flows-")
    old, b.db_path = b.db_path, lambda: os.path.join(tmp, "x.db")
    try:
        assert "error" in b.stock_flows()
        con = b.connect()
        con.execute("CREATE TABLE IF NOT EXISTS big_moves_run (stamp TEXT)")
        con.execute("INSERT INTO big_moves_run VALUES ('2026-09-30:95:t|tickers|r5|1')")
        con.execute("INSERT INTO quarters (quarter, filers) VALUES ('2026-09-30', 95)")
        con.execute("""CREATE TABLE IF NOT EXISTS big_moves (
            cik TEXT, manager TEXT, quarter TEXT, prior_quarter TEXT, cusip TEXT, issuer TEXT,
            ticker TEXT, kind TEXT, shares REAL, prior_shares REAL, flow REAL, value REAL,
            book_value REAL, position_count INTEGER, prior_book_value REAL, accession TEXT, filed TEXT)""")

        def add(manager, issuer, ticker, kind, flow, q="2026-09-30", count=50):
            con.execute("INSERT INTO big_moves VALUES ('1',?,?,'','c',?,?,?,1,0,?,1,1,?,1,'a','2026-11-13')",
                        (manager, q, issuer, ticker, kind, flow, count))
        add("M", "Filler Corp", "FILL", "added", 1.0)
        con.execute("CREATE TABLE IF NOT EXISTS books (cik TEXT, quarter TEXT)")
        for i in range(100):
            con.execute("INSERT INTO books (cik, quarter) VALUES (?, '2026-06-30')", (str(i),))
            if i < 95:
                con.execute("INSERT INTO books (cik, quarter) VALUES (?, '2026-09-30')", (str(i),))
        # one verdict per stock, whichever way a filer spells it
        add("F1", "VANGUARD INDEX FDS", "VTI", "added", 9e9)
        add("F2", "VANGUARD INDEX FDS", "VTI", "added", 9e9)
        add("F3", "Veradermics Inc", "VTI", "added", 9e9)
        add("W", "AVALONBAY CMNTYS", "WV8", "added", 9e9)             # a foreign line
        add("V", "VERIZON COMMUNICATIONS INC", "VZ", "trimmed", -2e9)
        for i, f in enumerate((5e9, -1e9, 2e9)):
            add(f"A{i}", "Alpha Corp", "AAA", "added" if f > 0 else "trimmed", f)
        add("B", "Beta Corp", "BBB", "exited", -3e9)
        for i in range(6):
            add(f"N{i}", "Newly Listed ADR", "NEW", "new", 9e9)        # nobody held it before
        add("SELF CORP", "Self Corp", "SELF", "new", 9e9)             # a filer's own parent
        add("Q", "Quant Corp", "QQQQ", "added", 9e9, count=4000)      # an index-like book
        con.commit()
        con.close()
        day = "2026-11-25"
        # before the deadline, or with too few in: the season is said, no sums
        assert "95 of about 100 managers" in b.stock_flows(today="2026-11-10")["error"], b.stock_flows(today="2026-11-10")
        con = b.connect()
        con.execute("INSERT INTO books (cik, quarter) VALUES ('1', '2026-12-31')")
        con.commit()
        assert "13F season: 1 of about 95" in b.stock_flows(today="2027-01-20")["error"]
        con.execute("DELETE FROM books WHERE quarter = '2026-12-31'")
        con.commit()
        con.close()
        # the filings are in but the scan is from before most of them
        con = b.connect()
        for stamp, ok in (("2026-09-30:40:t|x", False), ("2026-06-30:100:t|x", False), ("incomplete|2026-09-30:94:t|x", True)):
            con.execute("UPDATE big_moves_run SET stamp = ?", (stamp,))
            con.commit()
            got = b.stock_flows(today=day)
            assert ("error" not in got) == ok and (ok or "behind its filings" in got["error"]), (stamp, got)
        # ...also when the sync was cut short before it counted them
        con.execute("UPDATE big_moves_run SET stamp = '2026-09-30:60:t|x'")
        con.execute("UPDATE quarters SET filers = 60")
        con.commit()
        assert "behind its filings" in b.stock_flows(today=day)["error"]
        con.execute("UPDATE big_moves_run SET stamp = '2026-09-30:95:t|x'")
        con.execute("UPDATE quarters SET filers = 95")
        con.execute("INSERT INTO books (cik, quarter) VALUES ('9', '2031-09-30')")  # a mistyped period
        con.commit()
        con.close()
        f = b.stock_flows(today=day)
        assert f["managers"] == 95 and f["quarter"] == "2026-09-30" and f["filed_to"] == "2026-11-13", f
        assert [(d["ticker"], d["net"], d["adds"], d["trims"]) for d in f["added"][:1]] == [("AAA", 6e9, 2, 1)], f
        assert {d["ticker"] for d in f["added"]} == {"AAA", "FILL"} and [d["ticker"] for d in f["trimmed"]] == ["BBB", "VZ"], f
    finally:
        b.db_path = old


# ── the build ────────────────────────────────────────────────────────────────

def _generator():
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
    import weekly_brief
    return weekly_brief


def test_a_build_shows_its_steps_and_the_page_as_it_comes():
    import json
    import tempfile
    import threading
    g = _generator()
    path = os.path.join(tempfile.mkdtemp(prefix="progress-"), "progress.json")
    read = lambda: json.load(open(path, encoding="utf-8"))
    go = threading.Event()
    saved = {k: getattr(g, k) for k in ("fetch_macro_series", "fetch_macro_rows", "screener_doc", "clusters_data",
                                        "ipo_week", "earnings_week", "holdings_week")}
    seen = {}

    def filings(scan, progress=None, waiting=None):
        if not scan:
            return ROWS[:2], [], True
        progress(date(2026, 11, 20), 340, 512, 2)
        go.wait(10)
        return ROWS, [], True

    def calendar(week, say=None):
        raise OSError("down")  # one part failing
    g.fetch_macro_series = lambda today: {**_series(), **_regime_series(95.0, 25.0)}
    g.fetch_macro_rows = calendar
    g.screener_doc = lambda say=None, timeout=0: (say("Downloading prices 3/9", 1 / 3), (_doc(), None))[1]
    g.clusters_data = filings
    g.ipo_week = lambda today: IPO
    g.earnings_week = lambda today: EARN
    g.holdings_week = lambda symbols, *a: {"symbols": len(symbols), "rows": MINE}
    import sec_13f_bulk
    flows, sec_13f_bulk.stock_flows = sec_13f_bulk.stock_flows, lambda symbols=(): FLOWS
    out = {}
    try:
        t = threading.Thread(target=lambda: out.update(g.build_brief(date(2026, 11, 22), progress_path=path,
                                                                     symbols=["SECRETCO", "AAPL"])))
        t.start()
        import time
        for _ in range(100):
            time.sleep(0.05)
            if os.path.exists(path):
                seen = read()
                if sum(s["state"] in ("done", "warn") for s in seen["steps"]) >= 5:
                    break
        # everything but the filings is there, and says what is being worked on
        by = {s["id"]: s for s in seen["steps"]}
        assert by["filings"]["state"] == "run" and by["page"]["state"] == "wait", by
        assert by["calendar"]["state"] == "warn" and by["economy"]["state"] == "done", by
        assert "| 10-year Treasury |" in seen["body"] and "| Screen leaders |" in seen["body"], seen["body"]
        # what is known of the filings, and that more is coming
        assert "| S1 |" in seen["body"] and "| S3 |" not in seen["body"] and "_Working…_" in seen["body"], seen["body"]
        go.set()
        t.join(10)
        assert "| S3 |" in "\n".join(out["body"]) and "_Working…_" not in "\n".join(out["body"]), out["body"]
        assert "could not be read" in "\n".join(out["body"])  # the calendar, said on the page
        assert out["screens"] and out["rows"] == ROWS and out["complete"]
        end = read()
        assert not end["done"] and [s["state"] for s in end["steps"]][-1] == "run"  # the file is the caller's to finish
        out["progress"].finish()
        assert read()["done"]
        # the finished page is the one built in a single go
        mon, fri = wb.week_ahead(date(2026, 11, 22))
        want = wb.brief_body(g.ALL_TOPICS, "standard", g.fetch_macro_series(None), _doc(), None, ROWS, [], FLOWS, [], mon,
                             fri, "the release calendar could not be read (OSError)", date(2026, 11, 22), ipo=IPO, earnings=EARN)
        assert out["body"] == want
        # the owner's names are the build's to show, and no part of the page
        assert "SECRETCO" in read()["body"] and "SECRETCO" not in "\n".join(out["body"]), read()["body"]
        assert "SECRETCO" in "\n".join(wb.mine_part(out["mine"])) and "SECRETCO" not in str(out["problems"])
        assert out["problems"] == ["calendar: OSError"], out["problems"]
    finally:
        go.set()
        for k, v in saved.items():
            setattr(g, k, v)
        sec_13f_bulk.stock_flows = flows


def test_filings_that_fail_make_an_incomplete_week():
    g = _generator()
    saved = g.clusters_data
    g.clusters_data = lambda *a, **k: (_ for _ in ()).throw(OSError("down"))
    try:
        b = g.build_brief(date(2026, 11, 22), topics=["insider"], extras=False)
        assert not b["complete"] and "could not be read" in b["notes"][0], b["notes"]
    finally:
        g.clusters_data = saved


def test_the_calendar_is_asked_rarely_and_kept_when_it_refuses():
    import io
    import json
    import tempfile
    import time
    import urllib.error
    import urllib.request
    tmp, calls = tempfile.mkdtemp(prefix="cal-"), []
    saved, wb._calendar_cache = wb._calendar_cache, lambda name: os.path.join(tmp, name)
    real = urllib.request.urlopen
    week = (date(2026, 11, 16), date(2026, 11, 20))
    age = lambda hours: [os.utime(os.path.join(tmp, n), (0, time.time() - hours * 3600)) for n in os.listdir(tmp)]
    answer = [ff("CPI m/m", "2026-11-18T08:30:00-05:00")]

    def ok(req, timeout=0):
        calls.append(req.full_url)
        return io.BytesIO(json.dumps(answer).encode())

    def refuse(req, timeout=0):
        calls.append(req.full_url)
        raise urllib.error.HTTPError(req.full_url, 429, "", {}, None)
    try:
        urllib.request.urlopen = ok
        rows, failed = wb.fetch_macro_rows(week)
        assert len(rows) == 2 and not failed and len(calls) == 2
        wb.fetch_macro_rows(week)
        assert len(calls) == 2  # kept
        # kept, but not for the week asked for: the feed is asked
        wb.fetch_macro_rows((date(2026, 11, 23), date(2026, 11, 27)))
        assert len(calls) == 4
        # an empty answer is not kept over a good one
        good, answer[:] = list(answer), []
        age(4)
        wb.fetch_macro_rows(week)
        answer[:] = good
        urllib.request.urlopen = refuse
        rows, failed = wb.fetch_macro_rows(week)  # refused: the last answer, and no wait
        assert rows and not failed, (rows, failed)
        # one file answers with the week, the other refuses: its old copy is not mixed in
        answer[:] = [ff("CPI m/m", "2026-11-18T10:30:00-05:00")]
        urllib.request.urlopen = lambda req, timeout=0: (ok if "thisweek" in req.full_url else refuse)(req, timeout)
        rows, failed = wb.fetch_macro_rows(week)
        assert [r["date"] for r in rows] == ["2026-11-18T10:30:00-05:00"] and not failed, rows
        urllib.request.urlopen = refuse
        age(4 * 24)
        rows, failed = wb.fetch_macro_rows(week, retry_wait=0)
        assert not rows and "HTTP 429" in failed[0], failed
    finally:
        wb._calendar_cache, urllib.request.urlopen = saved, real


# ── earnings ahead, the owner's names ────────────────────────────────────────

EARN = {"rows": [{"symbol": "CRM", "company": "Salesforce, Inc.", "year_ago": date(2025, 12, 3)}], "unsure": 2,
        "partial": False, "calendar": []}
MINE = [{"symbol": "SECRETCO", "insiders": "2 bought $1.2M", "funds": "+$3.0M", "screens": "Buffett Value", "ahead": ""}]


def test_earnings_part_prints_no_vendor_date():
    text = "\n".join(wb.earnings_part(EARN))
    assert "| CRM | Salesforce, Inc. | Wed Dec 3 |" in text and "2 more not confirmed" in text, text
    assert "could not be read" in "\n".join(wb.earnings_part({"error": "could not be read (X)"}))
    assert "lists no large company" in "\n".join(wb.earnings_part({"rows": [], "unsure": 0, "partial": False}))
    assert "No large company" not in "\n".join(wb.earnings_part({"rows": [], "unsure": 0, "partial": True}))


def test_earnings_are_confirmed_by_the_same_report_a_year_ago():
    import sec_earnings as s
    day = date(2026, 12, 2)
    rows = [{"symbol": x, "date": d, "market_cap": c} for x, d, c in (
        ("BIG", "2026-12-02", 9e11), ("OTHER", "2026-12-02", 8e11), ("NOANS", "2026-12-02", 7e11),
        ("SMALL", "2026-12-02", 1e9), ("LATER", "2026-12-20", 9e11), ("NOCAP", "2026-12-02", None))]
    saved, s.reports = s.reports, lambda cik, cache: {
        "BIG": ("Big Inc.", [date(2026, 8, 27), date(2025, 12, 3), date(2025, 11, 10)]),
        "OTHER": ("Other Inc.", [date(2026, 10, 1), date(2025, 10, 1)]), "NOANS": None}[cik]
    try:
        got, unsure = s.ahead(rows, day, day + timedelta(days=7), None, lambda x: x)
        assert got == [{"symbol": "BIG", "company": "Big Inc.", "year_ago": date(2025, 12, 3)}] and unsure == 2, (got, unsure)
    finally:
        s.reports = saved


def test_mine_rows_and_block():
    doc = {"strategies": [{"id": "buffett", "name": "Buffett Value", "symbols": ["AAA"]},
                          {"id": "trend_template", "name": "TT", "symbols": ["AAA"]}]}
    rows = wb.mine_rows(["AAA", "BBB", "ZZZ"], {"AAA": {"insiders": 2, "value": 1.2e6}}, {"BBB": {"insiders": 1, "value": 5e5}},
                        {"AAA": {"net": -3e9}}, doc, {"AAA": ["earnings Wed Dec 2"]})
    assert [r["symbol"] for r in rows] == ["AAA", "BBB"], rows
    assert rows[0] == {"symbol": "AAA", "insiders": "2 bought $1.2M", "funds": "−$3.0B", "screens": "Buffett Value",
                       "ahead": "earnings Wed Dec 2"} and rows[1]["insiders"] == "1 sold $500.0K", rows
    assert "| AAA | 2 bought" in "\n".join(wb.mine_part({"symbols": 3, "rows": rows}))
    assert wb.mine_part(None) == [] and wb.mine_part({"symbols": 0}) == []
    none = "\n".join(wb.mine_part({"symbols": 4, "rows": [], "unread": ["funds", "screens"]}))
    assert "Nothing found" in none and "Not read: funds, screens." in none, none
    g = _generator()
    assert g.one_symbol("BRK-B") == g.one_symbol("BRK/B") == "BRK.B" and "BRK-B" in g.spellings(["BRK.B"])


def test_a_card_is_drawn_from_the_stance_and_public_figures():
    import tempfile
    g = _generator()
    r = wb.market_regime(_regime_series(95.0, 25.0), date(2026, 11, 22))
    tmp = __import__("pathlib").Path(tempfile.mkdtemp(prefix="card-"))
    rel = g.write_card(tmp, date(2026, 11, 22), {"regime": r, "series": _series()})
    if rel is not None:  # no Pillow or font: no picture
        assert rel == "assets/brief/2026-11-16.png" and (tmp / rel).read_bytes()[:4] == b"\x89PNG"
    assert g.write_card(tmp, date(2026, 11, 22), {"regime": None}) is None


# ── the public record ────────────────────────────────────────────────────────

def test_track_record():
    import brief_track as t
    from datetime import datetime, timezone
    at = lambda *a: datetime(*a, tzinfo=timezone.utc)
    issue = lambda key, close, state, stance: {"issue": key, "url": "u", "call": {
        "close": close, "state": state, "label": "L", "stance": stance, "line": "x"}}
    sun = at(2026, 11, 22, 17)
    d = t.add({"calls": []}, issue("W47", "2026-11-20", "bearish", 0.5), sun)
    d = t.add(d, issue("W47", "2026-11-20", "bearish", 0.5), sun)  # published again: one call
    assert len(d["calls"]) == 1 and "revised" not in d["calls"][0] and "line" not in d["calls"][0]
    d = t.add(d, issue("W47", "2026-11-25", "bullish", 1.0), at(2026, 11, 25, 15))
    d = t.add(d, issue("W47", "2026-11-25", "bullish", 1.0), at(2026, 11, 25, 16))
    assert d["calls"][0]["state"] == "bearish" and [r["state"] for r in d["calls"][0]["revised"]] == ["bullish"], d
    assert t.add(d, {"issue": "W48"}, sun) is d  # no stance, no call
    d = t.add(d, issue("W48", "2026-11-27", "bullish", 1.0), at(2026, 11, 29, 17))
    px = [(date(2026, 11, 20), 120.0), (date(2026, 11, 23), 100.0), (date(2026, 11, 27), 98.0), (date(2026, 11, 30), 96.0),
          (date(2026, 12, 1), 88.0)]
    a, b = t.outcomes(d, px)["calls"]
    # from Monday's close: the fall before it was public is no part of its record
    assert (a["since"], a["week"], a["quarter"], a["fall"]) == ("2026-11-23", "down", None, "yes"), a
    assert (b["since"], b["week"], b["fall"]) == ("2026-11-30", None, None), b
    rule = (1 + 0.5 * (98 / 100 - 1)) * (1 + 0.5 * (96 / 98 - 1)) * (88 / 96)
    total = t.outcomes(d, px)["total"]
    assert total["rule"] == round((rule - 1) * 100, 1) and total["always"] == -12.0, total
    # published in the session: that day's close; after it: the next one's
    in_session = t.outcomes(t.add({"calls": []}, issue("A", "x", "bearish", 0.5), at(2026, 11, 27, 17)), px)["calls"][0]
    after = t.outcomes(t.add({"calls": []}, issue("A", "x", "bearish", 0.5), at(2026, 11, 27, 19)), px)["calls"][0]  # a half day: closed at 18 UTC
    assert t._close(date(2026, 11, 27)).hour == 13 and t._close(date(2026, 11, 20)).hour == t._close(date(2026, 12, 23)).hour == 16
    assert in_session["since"] == "2026-11-27" and after["since"] == "2026-11-30"
    late = t.outcomes(t.add({"calls": []}, issue("A", "x", "bearish", 0.5), at(2026, 12, 2, 12)), px)
    assert late["calls"][0]["since"] is None and late["total"] == {}, late
    px += [(date(2026, 12, 2) + timedelta(days=i), 110.0) for i in range(70)]
    a = t.outcomes({"calls": d["calls"][:1]}, px)["calls"][0]
    assert (a["quarter"], a["fall"]) == ("up", "yes")
    import tempfile
    p = __import__("pathlib").Path(tempfile.mkdtemp(prefix="track-")) / "x" / "calls.json"
    kept = t.update(p, issue("W47", "2026-11-20", "bearish", 0.5), sun, [])
    assert kept["calls"][0]["issue"] == "W47" and p.exists()  # the call is kept without the index
    p.write_text("<<<<<<< HEAD")
    assert t.update(p, issue("W48", "x", "bearish", 0.5), sun, []) is None and p.read_text() == "<<<<<<< HEAD"


def test_what_the_monday_check_mails_about():
    g = _generator()
    assert g.serious("screens", "the scan timed out") and g.serious("funds", "OSError") and g.serious("economy", "12 series not read")
    for sid, warn in (("funds", "13F season: 120 of about 8,000 managers have filed for Sep 30, 2026"), ("earnings", "partly read"),
                      ("economy", "1 series not read"), ("calendar", "the calendar for that week was not published yet"),
                      ("holdings", "OSError")):
        assert not g.serious(sid, warn), (sid, warn)


# ── IPOs and lock-ups ────────────────────────────────────────────────────────

IPO = {"ahead": [{"company": "Alpha_Co", "ticker": "AAA", "range": (14.0, 16.0), "step": "8-A12B", "filed": "2026-11-20",
                  "spac": False},
                 {"company": "Blank Check", "ticker": "", "range": (10.0, 10.0), "step": "S-1/A", "filed": "2026-11-19",
                  "spac": True}],
       "lockups": [{"ticker": "QNT", "company": "Quantinuum Inc.", "dated": date(2026, 6, 3), "days": 180,
                    "ends": date(2026, 11, 30)}], "unread": 1}


def test_ipo_part():
    text = "\n".join(wb.ipo_part(IPO))
    assert "| AAA | Alpha\\_Co | $14.00–16.00 | 8-A12B, Nov 20 |" in text, text
    assert "| — | Blank Check (SPAC) | $10.00 | S-1/A, Nov 19 |" in text
    assert "| QNT | Quantinuum Inc. | Mon Nov 30 | 180 days |" in text and "1 lock-up not confirmed by the prospectus is" in text
    empty = "\n".join(wb.ipo_part({"ahead": [], "lockups": [], "unread": 0}))
    assert "No IPO is expected" in empty and "No lock-up is known" in empty and "|" not in empty
    # nothing read is not "none"
    blind = "\n".join(wb.ipo_part({"ahead": [], "ahead_unread": 2, "lockups": [], "unread": 0, "partial": True}))
    assert "No IPO" not in blind and "No lock-up" not in blind and "2 IPOs whose filings" in blind and "partly read" in blind
    assert "could not be read" in "\n".join(wb.ipo_part({"error": "could not be read (OSError)"}))


def test_what_a_filing_says():
    import sec_ipo as s
    assert s.price_range("the initial public offering price will be between $14.00 and $16.00 per share") == (14.0, 16.0)
    assert s.price_range("revenue was between $12.1 and $12.5 million. We expect the initial public offering price to be "
                         "between $18.00 and $20.00 per share") == (18.0, 20.0)
    assert s.price_range("a conversion price between $2.00 and $3.00. The offering price is between $8.00 and $10.00") == (8, 10)
    assert s.price_range("the offering price, with warrants to exercise between $0.50 and $1.20") is None
    assert s.price_range("offering price in the range of $ [ 5 ] to $[ 6 ] per Ordinary Share") == (5.0, 6.0)
    assert s.price_range("at an initial public offering price of $10.00. We have") == (10.0, 10.0)
    assert s.price_range("between $2 and $900 per share") is None and s.price_range("price $ to be set") is None
    assert s.prospectus_date("Alliance Prospectus Dated June 9, 2026. Table") == date(2026, 6, 9)
    assert s.prospectus_date("The date of this prospectus is June 2, 2026") == date(2026, 6, 2)
    assert s.prospectus_date("Subject to completion, dated , 2026") is None
    assert s.prospectus_date("preliminary prospectus dated October 28, 2026. The date of this prospectus is November 5, 2026")\
        == date(2026, 11, 5)
    assert s.symbol("approved under the symbol “QNT.”") == "QNT" and s.symbol("under the symbol “ ”") == ""
    lock = "Our directors and officers agreed not to sell any of our stock for 180 days following the date of this prospectus. "
    r144 = "Under Rule 144, beginning 90 days after the date of this prospectus, holders may not yet sell. "
    assert s.lockup_terms(lock) == [180] and s.lockup_terms(r144 * 5) == []
    assert s.lockup_terms(("x " * 400) + lock + ("y " * 400) + r144 * 3) == [180]
    both = lock + ("y " * 400) + lock.replace("180", "90")
    assert s.lockup_terms(both) == [90, 180]
    own = "we have agreed not to sell any shares for 90 days after the date of this prospectus. "
    assert s.lockup_terms(own * 2 + ("y " * 400) + lock) == [180]  # the company's own is not the holders'
    assert s.lockup_terms("we will not be required to pay for 120 days after the date of this prospectus") == []
    assert s.lockup_terms("underwriters have 30 days from the date of this prospectus to purchase") == []
    assert s.plain("<p>The&nbsp;date<br>of <b>this</b> prospectus") == "The date of this prospectus".replace("The", " The")


class _Kv(dict):
    def get(self, k, *_):
        return dict.get(self, k)

    def put(self, k, v):
        self[k] = v


def test_lockups_end_by_the_prospectus_and_only_when_it_is_sure():
    import sec_ipo as s
    f = [{"form": "424B4", "date": "2026-06-04", "accession": "1", "doc": "d"}]
    facts = {"A": {"dated": "2026-06-03", "lockup": [90, 180]}, "B": {"dated": None, "lockup": [180]},
             "C": {"dated": "2026-06-03", "lockup": [90]}, "E": {"dated": "2026-05-20", "lockup": [180]}}
    facts["A"]["symbol"] = "AAA"
    saved = s._filings, s._text
    s._filings = lambda cik, cache: (f, f"{cik} Inc.") if cik != "D" else (None, None)
    s._text = lambda cik, row, cache: facts[cik]
    row = lambda c: {"cik": c, "ticker": c, "company": c, "priced_date": "2026-06-04", "expiry": "2026-12-01",
                     "lockup_days": 180, "basis": "term"}
    try:
        got, unread = s.lockups([row(c) for c in "ABCDE"], date(2026, 11, 30), date(2026, 12, 6), _Kv())
        # from the prospectus's own date, a day before the pricing row's
        assert [(r["ticker"], r["company"], r["ends"]) for r in got] == [("AAA", "A Inc.", date(2026, 11, 30))], got
        assert unread == 4  # no date; another term; SEC did not answer; another prospectus's date
        assumed = dict(row("A"), basis="assumed")  # no term in the calendar: the prospectus must state just one
        assert s.lockups([assumed], date(2026, 11, 30), date(2026, 12, 6), _Kv()) == ([], 1)
        got, _ = s.lockups([row("A")], date(2026, 12, 1), date(2026, 12, 7), _Kv())
        assert got == []  # ended the day before the window, whatever the calendar's date
        deals = [{"cik": "1", "company": "N", "ticker": "T", "projection": {"kind": k, "date": "2026-12-02"}}
                 for k in ("scheduled", "estimate")] + [{"cik": "1", "company": "N", "ticker": "late",
                                                         "projection": {"kind": "scheduled", "date": "2026-12-20"}},
                                                        {"company": "no CIK", "projection": {"kind": "scheduled",
                                                                                             "date": "2026-12-02"}}]
        s._filings = lambda cik, cache: ([{"form": "8-A12B", "date": "2026-11-28", "accession": "3", "doc": ""},
                                          {"form": "S-1/A", "date": "2026-11-27", "accession": "2", "doc": "d"},
                                          {"form": "S-1/A", "date": "2026-11-20", "accession": "1", "doc": "d"}], "Sec Name Inc.")
        s._text = lambda cik, r, cache: {"range": (14.0, 16.0) if r["accession"] == "1" else None, "symbol": "T"}
        got, unread = s.ahead(deals, date(2026, 11, 30), date(2026, 12, 6), _Kv())
        assert got == [{"company": "Sec Name Inc.", "ticker": "T", "range": (14.0, 16.0), "step": "8-A12B",
                        "filed": "2026-11-28", "spac": False}] and unread == 1, (got, unread)
    finally:
        s._filings, s._text = saved


# ── the issue and its posts ──────────────────────────────────────────────────

def _body(topics, **kw):
    mon, fri = date(2026, 11, 16), date(2026, 11, 20)
    ev = wb.macro_events([ff("CPI m/m", "2026-11-18T08:30:00-05:00")], mon, fri)
    return "\n".join(wb.brief_body(topics, "standard", _series(), _doc(), None, ROWS, [], FLOWS, ev, mon, fri, None, date(2026, 11, 22), **kw))


def test_the_issue_holds_the_chosen_sections_in_order_and_is_short():
    full = _body(("summary", "macro", "screens", "insider", "funds"))
    heads = [ln for ln in full.split("\n") if ln.startswith("## ")]
    assert heads == ["## At a glance", "## Markets and economy", "## Week ahead, Nov 16–20", "## Stocks"], heads
    assert "## Week ahead" in _body(("ipo",)) and "IPOs and lock-ups" in _body(("ipo",))
    parts = [full.index(x) for x in ("| Insider buying |", "| Funds added |", "| Screen leaders |")]
    assert parts == sorted(parts) and parts[0] > full.index("## Stocks"), parts
    assert full.rstrip().endswith("Made with [finterm](https://yumima.github.io/finterm/), an open-source research terminal.")
    assert len(full.split()) < 900 and len(full.split("\n")) < 100, (len(full.split()), len(full.split("\n")))
    only = _body(("funds",))
    assert "## Stocks" in only and "| Funds added |" in only and "Insider" not in only and "CPI" not in only


def test_issue_disclaimer_names_every_source_insider_page_keeps_its_own():
    for src in ("ForexFactory", "TradingView", "FRED", "13F", "Form"):
        assert src in wb.ISSUE_DISCLAIMER, src
    assert "ForexFactory" not in wb.DISCLAIMER and wb.DISCLAIMER.endswith("Source: U.S. SEC, EDGAR.")


def test_posts_fit_and_announce_only_what_the_issue_holds():
    url = "https://yumima.github.io/finterm/brief/2026-11-16/"
    today = date(2026, 11, 22)
    full = wb.social_posts(ROWS, today, url, series=_series(), flows=FLOWS)
    for net in ("x", "bluesky", "mastodon"):
        limit, cost = wb._limit(net, url)
        weigh = wb.x_weight if net == "x" else len
        assert weigh(full[net]) - weigh(url) + cost <= limit and url in full[net], (net, full[net])
    assert full["x"].count("$S") == 1 and "10Y 4.50%" in full["x"], full["x"]
    call = wb.social_posts(ROWS, today, url, series={**_series(), **_regime_series(95.0, 25.0)}, flows=FLOWS)["x"]
    assert "Bearish: S&P 500 below its 200-day average, fear elevated (Nov 20)" in call and wb.x_weight(call) - len(url) + 23 <= 280, call  # one cashtag on X
    assert "Funds added, net: $INTC" in full["bluesky"], full["bluesky"]
    bare = wb.social_posts(ROWS, today, url, topics=("macro",), series=_series(), flows=FLOWS)
    for net in ("x", "bluesky", "mastodon"):
        assert "nsider" not in bare[net] and "Funds" not in bare[net] and "screen" not in bare[net], bare[net]
    # the page says the screens did not run: the post does not announce them
    assert "screen" not in wb.social_posts(ROWS, today, url, series=_series(), flows=FLOWS, screens=False)["mastodon"]
    assert "CPI" not in full["bluesky"] and "jobless rate 4.1%" in full["bluesky"], full  # the newest month has no y/y
    nothing = wb.social_posts([], today, url, topics=("macro",))
    assert url in nothing["x"] and "10Y" not in nothing["x"]


def main():
    for t in [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]:
        try:
            t()
            print(f"ok   {t.__name__}")
        except AssertionError as e:
            print(f"FAIL {t.__name__}: {e}")
            FAILURES.append(t.__name__)
        except Exception:
            import traceback
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all weekly brief tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
