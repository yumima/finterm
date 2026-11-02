#!/usr/bin/env python3
"""IPO Watch calendar: one row per deal, and a projected pricing date that
says what it rests on.

The screen used to read Nasdaq's four buckets as four sets of rows. The same
dealID sits in `filed` (month of first filing) and later in `priced` or
`withdrawn`, so a deal that priced in June stayed on the pipeline as "FILED"
from May, and withdrawals were never subtracted. Filed rows carry no date but
the filing date, so the pipeline had no projected IPO date at all.

No network: the parsers and the model are pure; Nasdaq/EDGAR responses are
shaped from live samples taken 2026-11-01.
"""

import os
import sys
from datetime import date, timedelta

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import ipo_calendar as ic  # noqa: E402

FAILURES = []
T = date(2026, 11, 1)


def month(filed=(), upcoming=(), priced=(), withdrawn=()):
    return {"filed": list(filed), "upcoming": list(upcoming),
            "priced": list(priced), "withdrawn": list(withdrawn)}


def test_merge_by_deal_id_one_row_per_deal():
    may = month(filed=[{"dealID": "1-1", "companyName": "Wilco 63 Corp", "proposedTickerSymbol": "WILC",
                        "filedDate": "5/29/2026", "dollarValueOfSharesOffered": "$100,000,000"}])
    jun = month(priced=[{"dealID": "1-1", "companyName": "Wilco 63 Corp", "proposedTickerSymbol": "WILC",
                         "proposedExchange": "NASDAQ Global", "proposedSharePrice": "10.00",
                         "sharesOffered": "10,000,000", "pricedDate": "6/18/2026",
                         "dollarValueOfSharesOffered": "$100,000,000"}])
    deals = ic.merge_months([may, jun])
    assert len(deals) == 1, deals
    d = deals["1-1"]
    assert d["status"] == "priced"
    assert d["filed_date"] == date(2026, 5, 29) and d["priced_date"] == date(2026, 6, 18)
    assert d["final_price"] == 10.0 and d["shares"] == 10_000_000


def test_withdrawn_bucket_ends_the_deal():
    may = month(filed=[{"dealID": "2-1", "companyName": "Coolbit", "filedDate": "5/22/2026"}])
    aug = month(withdrawn=[{"dealID": "2-1", "companyName": "Coolbit", "filedDate": "5/22/2026",
                            "withdrawDate": "8/31/2026"}])
    d = ic.merge_months([may, aug])["2-1"]
    assert d["status"] == "withdrawn" and d["withdrawn_date"] == date(2026, 8, 31)


def test_refiling_after_withdrawal_is_a_separate_live_deal():
    # Kepler: an old deal withdrawn May 19, a new dealID filed May 22.
    may = month(filed=[{"dealID": "3-new", "companyName": "Kepler", "filedDate": "5/22/2026"}],
                withdrawn=[{"dealID": "3-old", "companyName": "Kepler", "filedDate": "9/03/2025",
                            "withdrawDate": "5/19/2026"}])
    deals = ic.merge_months([may])
    assert deals["3-new"]["status"] == "filed" and deals["3-old"]["status"] == "withdrawn"


def test_priced_range_is_not_a_final_price():
    jun = month(priced=[{"dealID": "4", "companyName": "X", "proposedSharePrice": "8.00-10.00",
                         "pricedDate": "6/1/2026"}])
    assert ic.merge_months([jun])["4"]["final_price"] is None


def test_absent_is_none_not_zero():
    assert ic.parse_money("--") is None and ic.parse_money("") is None and ic.parse_money("$0") is None
    assert ic.parse_money("$386,400,000") == 386_400_000
    assert ic.parse_range("$19.00-21.00") == (19.0, 21.0)


def test_months_are_history_plus_next_month_only():
    ms = ic.months_to_read(T)
    assert ms[-1] == "2026-12" and ms[-2] == "2026-11" and ms[0] == "2025-10", ms
    assert len(ms) == ic.MONTHS_BACK + 2


def test_spac_classification():
    assert ic.is_spac("Anything", sic="6770")
    assert not ic.is_spac("Acquisition Holdings Inc", sic="7372")   # SIC beats the name
    assert ic.is_spac("BHAV Acquisition Corp II")
    assert ic.is_spac("Cantor Equity Partners VII", ticker="CEPVU", price=10.0)
    assert not ic.is_spac("OPAY Ltd", ticker="OPAY", price=20.0)


def sub(rows, sic="7372"):
    """rows: (form, date) or (form, date, file_no)."""
    return {"sic": sic, "filings": {"recent": {
        "form": [r[0] for r in rows], "filingDate": [r[1] for r in rows],
        "accessionNumber": [f"acc{i}" for i in range(len(rows))],
        "primaryDocument": ["d.htm"] * len(rows),
        "fileNumber": [r[2] if len(r) > 2 else "" for r in rows]}}}


OPAY = sub([("EFFECT", "2026-10-29"), ("3", "2026-10-29"), ("CERT", "2026-10-27"),
            ("8-A12B", "2026-10-26"), ("F-1/A", "2026-10-23"), ("F-1", "2026-10-09"),
            ("DRS/A", "2026-08-14"), ("DRS", "2026-02-12")])


def test_ladder_keeps_only_registration_steps():
    lad = ic.ladder_from_submissions(OPAY, date(2026, 10, 9))
    forms = [r["form"] for r in lad]
    assert "3" not in forms and forms[0] == "EFFECT" and "DRS" in forms, forms


def test_ladder_ignores_an_earlier_abandoned_attempt():
    s = sub([("S-1", "2026-09-01"), ("424B4", "2024-03-01"), ("S-1", "2024-02-01")])
    lad = ic.ladder_from_submissions(s, date(2026, 9, 1))
    assert [r["form"] for r in lad] == ["S-1"], lad
    assert ic.stage_of(lad)[0] == "on_file"


def test_stage_effective_and_imminent_window():
    lad = ic.ladder_from_submissions(OPAY, date(2026, 10, 9))
    st, sd = ic.stage_of(lad)
    assert st == "effective" and sd == date(2026, 10, 29)
    d = {"status": "filed", "stage": st, "stage_date": sd.isoformat(), "filed_date": date(2026, 10, 9)}
    p = ic.project(d, None, date(2026, 10, 30))
    assert p["kind"] == "imminent" and p["hi"] == "2026-10-31", p
    # two days past the window: overdue, not a date
    assert ic.project(d, None, date(2026, 11, 2))["kind"] == "overdue"
    assert ic.project(d, None, date(2026, 12, 15))["kind"] == "stalled"


def test_old_effect_before_a_new_amendment_is_not_imminent():
    s = sub([("S-1/A", "2026-10-20"), ("EFFECT", "2026-06-01"), ("S-1", "2026-05-01")])
    assert ic.stage_of(ic.ladder_from_submissions(s, date(2026, 5, 1)))[0] == "on_file"


def test_rw_withdraws_but_a_later_amendment_revives():
    s = sub([("RW", "2026-10-01"), ("S-1/A", "2026-09-01"), ("S-1", "2026-05-01")])
    assert ic.stage_of(ic.ladder_from_submissions(s, date(2026, 5, 1)))[0] == "withdrawn"
    s = sub([("S-1/A", "2026-10-05"), ("RW", "2026-10-01"), ("S-1", "2026-05-01")])
    assert ic.stage_of(ic.ladder_from_submissions(s, date(2026, 5, 1)))[0] == "on_file"


def test_rw_against_an_older_registration_does_not_kill_the_live_deal():
    s = sub([("RW", "2026-05-25", "333-100"), ("S-1", "2026-05-22", "333-200")])
    assert ic.stage_of(ic.ladder_from_submissions(s, date(2026, 5, 22)))[0] == "on_file"
    s = sub([("RW", "2026-05-25", "333-200"), ("S-1", "2026-05-22", "333-200")])
    assert ic.stage_of(ic.ladder_from_submissions(s, date(2026, 5, 22)))[0] == "withdrawn"


def test_future_scheduled_date_beats_a_lapsed_edgar_window():
    d = {"status": "upcoming", "expected_date": T + timedelta(days=3), "stage": "listing",
         "stage_date": (T - timedelta(days=12)).isoformat(), "filed_date": T - timedelta(days=60)}
    assert ic.project(d, None, T)["kind"] == "scheduled"


def test_no_filing_date_still_bounds_the_ladder():
    # Uplisting: only an upcoming row in the window; a 2023 424B4 is history.
    d = {"deal_id": "u", "company": "U", "status": "upcoming", "filed_date": None,
         "expected_date": date(2026, 11, 10)}
    ic.apply_submissions(d, sub([("S-1/A", "2026-10-20"), ("S-1", "2026-06-01"), ("424B4", "2023-03-01")]), date(2026, 11, 1))
    assert d["status"] == "upcoming" and d["stage"] == "on_file", d


def test_edgar_424b4_overrides_a_lagging_calendar():
    d = {"deal_id": "x", "company": "X", "status": "filed", "filed_date": date(2026, 10, 1)}
    ic.apply_submissions(d, sub([("424B4", "2026-10-30"), ("EFFECT", "2026-10-29"), ("S-1", "2026-10-01")]), date(2026, 11, 1))
    assert d["status"] == "priced" and d["priced_from"] == "edgar" and d["priced_date"] == date(2026, 10, 30)
    p = ic.project(d, None, T)
    assert p["kind"] == "priced" and "424B" in p["basis"], p   # not "Nasdaq priced row"


def test_scheduled_uses_nasdaq_date_and_a_past_date_is_overdue():
    d = {"status": "upcoming", "expected_date": date(2026, 11, 5), "filed_date": date(2026, 10, 1)}
    assert ic.project(d, None, T) == {"kind": "scheduled", "date": "2026-11-05",
                                      "basis": ic.project(d, None, T)["basis"]}
    d["expected_date"] = date(2026, 10, 30)
    assert ic.project(d, None, T)["kind"] == "overdue"


def _cohort(n_priced, n_withdrawn, n_pending, lag, spac=False):
    """Deals filed 200 days before T: `n_priced` priced `lag` days after."""
    deals = {}
    fd = T - timedelta(days=200)
    for i in range(n_priced):
        deals[f"p{i}"] = {"filed_date": fd, "priced_date": fd + timedelta(days=lag + i), "spac": spac}
    for i in range(n_withdrawn):
        deals[f"w{i}"] = {"filed_date": fd, "withdrawn_date": fd + timedelta(days=90), "spac": spac}
    for i in range(n_pending):
        deals[f"q{i}"] = {"filed_date": fd, "spac": spac}
    return deals


def test_comparables_are_conditional_on_age_and_class():
    deals = _cohort(10, 5, 5, lag=20)
    deals["late"] = {"filed_date": T - timedelta(days=200), "priced_date": T - timedelta(days=100),
                     "spac": False}
    deals["spac"] = {"filed_date": T - timedelta(days=200), "priced_date": T - timedelta(days=150),
                     "spac": True}
    me = {"filed_date": T - timedelta(days=60), "spac": False}
    deals["me"] = me
    c = ic.comparables(deals, me, T)
    # at age 60 the 10 early pricers (lags 20–29) are gone; the late one
    # (lag 100) and the 5 still waiting remain, plus 5 withdrawals at day 90.
    assert c["at_risk"] == 11 and c["priced"] == 1 and c["withdrawn"] == 5 and c["pending"] == 5, c
    assert c["lag_q50"] == 100


def test_estimate_window_and_long_shot():
    deals = _cohort(20, 5, 5, lag=20)
    me = {"filed_date": T - timedelta(days=5), "spac": False, "status": "filed"}
    deals["me"] = me
    p = ic.project(me, ic.comparables(deals, me, T), T)
    assert p["kind"] == "estimate" and p["lo"] >= T.isoformat() and p["lo"] <= p["date"] <= p["hi"], p
    assert abs(p["share"] - 20 / 30) < 1e-9
    deals = _cohort(6, 30, 30, lag=20)
    deals["me"] = me
    assert ic.project(me, ic.comparables(deals, me, T), T)["kind"] == "long_shot"


def test_dormant_beats_estimate():
    me = {"filed_date": T - timedelta(days=300), "status": "filed",
          "last_filing_date": (T - timedelta(days=ic.DORMANT_DAYS + 1)).isoformat()}
    assert ic.project(me, None, T)["kind"] == "dormant"


def test_lockup_uses_the_deal_term_and_skips_spacs():
    deals = {
        "a": {"deal_id": "a", "ticker": "AAA", "company": "A", "priced_date": T - timedelta(days=80),
              "lockup_days": 90, "spac": False},
        "b": {"deal_id": "b", "ticker": "BBB", "company": "B", "priced_date": T - timedelta(days=170),
              "lockup_days": None, "spac": False},
        "c": {"deal_id": "c", "ticker": "CCCU", "company": "C Acquisition", "priced_date": T - timedelta(days=170),
              "lockup_days": 180, "spac": True},
        "d": {"deal_id": "d", "ticker": "DDD", "company": "D", "priced_date": T - timedelta(days=80),
              "lockup_days": 180, "spac": False},
    }
    rows = {r["ticker"]: r for r in ic.lockup_rows(deals, T)}
    assert set(rows) == {"AAA", "BBB", "DDD"}, rows
    assert rows["AAA"]["expiry"] == (T + timedelta(days=10)).isoformat() and rows["AAA"]["basis"] == "term"
    assert rows["BBB"]["basis"] == "assumed" and rows["BBB"]["lockup_days"] == 180


def test_overview_dashes_are_absent():
    po = {"SECCIK": {"value": "0002103076"}, "LockupPeriodNumberofDays": {"value": "--"},
          "ProposedSharePrice": {"value": "$19.00-21.00"}, "SharesOffered": {"value": 16000000.0},
          "SharesOverAllotment": {"value": "2,400,000"}}
    ov = ic.parse_overview(po)
    assert ov["cik"] == "2103076" and ov["lockup_days"] is None and ov["shares"] == 16_000_000
    assert ov["overallot"] == 2_400_000


def test_month_copy_taken_while_open_is_not_trusted_for_three_days():
    nov1 = date(2026, 11, 1)
    oct31_10am = ic._month_end_epoch("2026-10") - 14 * 3600
    assert ic._month_ttl("2026-10", nov1, oct31_10am) == 30 * 60
    assert ic._month_ttl("2026-10", nov1, ic._month_end_epoch("2026-10") + 2 * 86400) == 3 * 86400
    assert ic._month_ttl("2026-11", nov1, None) == 30 * 60


def test_pending_overview_is_reread_once_the_deal_prices():
    import tempfile
    c = ic.Cache(os.path.join(tempfile.mkdtemp(), "c.sqlite"))
    c.put("ov:123", {"lockup_days": None})          # cached while pending (no _final)
    calls = []
    real = ic._fetch_overview_net
    ic._fetch_overview_net = lambda did: calls.append(did) or {"lockup_days": 90}
    try:
        assert ic.overviews(c, [("123", False)])["123"] == {"lockup_days": None}
        assert calls == []
        got = ic.overviews(c, [("123", True)])["123"]
        assert calls == ["123"] and got["lockup_days"] == 90 and got["_final"]
        calls.clear()
        assert ic.overviews(c, [("123", True)])["123"]["lockup_days"] == 90 and calls == []
    finally:
        ic._fetch_overview_net = real


def test_ladder_floor_uses_the_as_of_date():
    d = {"deal_id": "u", "company": "U", "status": "filed", "filed_date": None, "expected_date": None}
    # As of 2026-06-01 the floor is 2025-06-01: a 2025-07 S-1 is in the
    # ladder (the real date would have cut it off).
    ic.apply_submissions(d, sub([("S-1", "2025-07-01")]), date(2026, 6, 1))
    assert d["stage"] == "on_file" and len(d["ladder"]) == 1, d


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    for t in tests:
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
    print("all ipo calendar tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
