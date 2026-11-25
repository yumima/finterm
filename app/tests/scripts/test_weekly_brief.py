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
    ev = wb._load_evidence()  # the shipped record holds every state the call can make
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


# ── the issue and its posts ──────────────────────────────────────────────────

def _body(topics, **kw):
    mon, fri = date(2026, 11, 16), date(2026, 11, 20)
    ev = wb.macro_events([ff("CPI m/m", "2026-11-18T08:30:00-05:00")], mon, fri)
    return "\n".join(wb.brief_body(topics, "standard", _series(), _doc(), None, ROWS, [], FLOWS, ev, mon, fri, None, date(2026, 11, 22), **kw))


def test_the_issue_holds_the_chosen_sections_in_order_and_is_short():
    full = _body(("summary", "macro", "screens", "insider", "funds"))
    heads = [ln for ln in full.split("\n") if ln.startswith("## ")]
    assert heads == ["## At a glance", "## Markets and economy", "## Week ahead, Nov 16–20", "## Stocks"], heads
    parts = [full.index(x) for x in ("| Insider buying |", "| Funds added |", "| Screen leaders |")]
    assert parts == sorted(parts) and parts[0] > full.index("## Stocks"), parts
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
