#!/usr/bin/env python3
"""Weekly brief: the week-ahead macro section and the top name per screen.

Both go to the PUBLIC site, so beyond "is it right" the tests pin what must
never be written: the calendar feed's consensus figures, and any TradingView /
Yahoo number behind a screen's ranking (their terms forbid redistribution).
No network.
"""

import os
import sys
from datetime import date

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


def test_macro_section_never_prints_consensus():
    rows = [ff("CPI m/m", "2026-11-18T08:30:00-05:00", forecast="0.37%", previous="0.41%")]
    text = "\n".join(wb.macro_section(wb.macro_events(rows, date(2026, 11, 16), date(2026, 11, 20)),
                                      date(2026, 11, 16), date(2026, 11, 20)))
    assert "Wed Nov 18" in text and "8:30 AM" in text and "| CPI m/m |" in text, text
    assert "0.37" not in text and "0.41" not in text, text


def test_macro_failure_is_said_not_an_empty_week():
    text = "\n".join(wb.macro_section([], date(2026, 11, 16), date(2026, 11, 20), error="feed down"))
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


# ── top name per screen ──────────────────────────────────────────────────────

def _doc():
    ev = {"tested": True, "excess": 0.0174, "t": 0.51}
    return {
        "asof": "2026-11-13",
        "strategies": [
            {"id": "trend_template", "name": "Minervini Trend Template", "symbols": ["AAA", "BBB"], "evidence": ev},
            {"id": "rs_leaders", "name": "RS Leaders", "symbols": ["AAA", "BBB"], "evidence": ev,
             "summary": "The 25 highest-RS stocks.", "rank_by": "RS rating"},
            {"id": "momentum", "name": "Momentum 12-1", "symbols": ["AAA", "CCC"], "evidence": ev,
             "summary": "Past 12-month return.", "rank_by": "12-1 return"},
            {"id": "canslim", "name": "CAN SLIM (partial)", "symbols": [], "evidence": {"tested": False}},
            {"id": "luk_pullback", "name": "Leader Pullback", "symbols": ["DDD"],
             "evidence": {"tested": True, "avg_R": -0.17, "trades": 1529, "excess": -0.2, "t": -5.7}},
        ],
        "rows": {"AAA": {"name": "Alpha Corp", "price": 123.45, "rs": 99, "r12m": 0.8765},
                 "CCC": {"name": "Gamma_Inc [A]", "price": 67.89}, "DDD": {"name": "Delta", "price": 4.32}},
    }


def test_one_name_per_screen_template_folded_into_rs_leaders():
    picks = wb.screen_picks(_doc())
    got = [(st["id"], sym, rank) for st, sym, _, rank in picks]
    assert got == [("rs_leaders", "AAA", 1), ("momentum", "CCC", 2), ("canslim", None, 0),
                   ("luk_pullback", "DDD", 1)], got


def test_picks_section_carries_no_vendor_numbers():
    doc = _doc()
    text = "\n".join(wb.picks_section(wb.screen_picks(doc), asof=doc["asof"]))
    for vendor in ("123.45", "67.89", "4.32", "87.6", "0.8765"):
        assert vendor not in text, vendor
    assert "not a recommendation" in text
    assert "**AAA** Alpha Corp" in text and "(its #2)" in text
    assert "Gamma\\_Inc \\[A\\]" in text, text  # vendor text is never markup
    assert "no stock passed" in text
    assert "+1.7% a year vs the equal-weighted S&P 500 (t 0.5)." in text, text
    assert "average -0.17R per trade over 1,529 trades." in text, text
    assert "worse than random" not in text  # a comparison the export does not hold
    stale = "\n".join(wb.picks_section(wb.screen_picks(doc), asof=doc["asof"], stale=3))
    assert "Prices for 3 stocks could not be refreshed" in stale


def test_issue_disclaimer_names_every_source_insider_page_keeps_its_own():
    assert "ForexFactory" in wb.ISSUE_DISCLAIMER and "TradingView" in wb.ISSUE_DISCLAIMER
    assert "ForexFactory" not in wb.DISCLAIMER and wb.DISCLAIMER.endswith("Source: U.S. SEC, EDGAR.")
    assert "Dated Co." in "\n".join(wb.picks_section([({"name": "S", "evidence": {}}, "DC", "Dated Co.", 1)]))
    assert "Dated Co.." not in "\n".join(wb.picks_section([({"name": "S", "evidence": {}}, "DC", "Dated Co.", 1)]))


def test_week_in_brief_summarises_last_and_next_week():
    rows = [{"symbol": "AAA", "issuer": "Alpha", "value": 2.5e6}, {"symbol": "", "issuer": "Beta Holdings", "value": 1e6}]
    ev = wb.macro_events([ff("CPI m/m", "2026-11-18T08:30:00-05:00"), ff("CPI y/y", "2026-11-18T08:30:00-05:00"),
                          ff("Unemployment Claims", "2026-11-19T08:30:00-05:00", impact="Medium")],
                         date(2026, 11, 16), date(2026, 11, 20))
    picks = wb.screen_picks(_doc())
    text = "\n".join(wb.week_summary(rows, ev, date(2026, 11, 16), date(2026, 11, 20), None, picks))
    assert "2 companies had two or more insiders buying on the open market, $50K or more" in text and "AAA ($2.5M)" in text and "Beta Holdings" in text
    assert "Wed CPI; Thu Unemployment Claims" in text, text  # m/m and y/y read as one CPI
    assert "AAA (RS Leaders)" in text and "not a recommendation" in text
    text = "\n".join(wb.week_summary([], [], date(2026, 11, 16), date(2026, 11, 20), "feed down", [], "x"))
    assert "no company had" in text and "not available" in text and "not run" in text


def test_screen_failure_is_said():
    text = "\n".join(wb.picks_section([], error="the scan timed out"))
    assert "could not be run" in text and "timed out" in text


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
