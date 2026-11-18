#!/usr/bin/env python3
"""News > WHAT'S ON company events (scripts/whats_on_calendar.py).

No network: Nasdaq answers are shaped from live samples taken 2026-11-13.
The guards that matter: a refused or wrong-day answer is None (a failed day,
reported), not [] (a day without reports); a missing market cap or EPS is
None, never 0; the user's own names survive the size cut.
"""

import os
import sys
import tempfile
from datetime import date

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import whats_on_calendar as w  # noqa: E402

FAILURES = []
D = date(2026, 11, 18)


def day_doc(rows, as_of="Wed, Nov 18, 2026"):
    return {"data": {"asOf": as_of, "rows": rows}, "status": {"rCode": 200}}


ROW_PANW = {"lastYearRptDt": "11/19/2025", "lastYearEPS": "$0.53", "time": "time-after-hours", "symbol": "PANW",
            "name": "Palo Alto Networks, Inc.", "marketCap": "$264,078,852,000", "fiscalQuarterEnding": "Oct/2026",
            "epsForecast": "$0.52", "noOfEsts": "15"}
ROW_TJX = {"time": "time-pre-market", "symbol": "TJX", "name": "TJX Companies, Inc. (The)",
           "marketCap": "$149,574,468,000", "epsForecast": "$1.33", "noOfEsts": "7"}
ROW_SMALL = {"time": "time-not-supplied", "symbol": "TINY", "name": "Tiny Corp", "marketCap": "$900,000,000",
             "epsForecast": "($0.12)", "noOfEsts": "2"}
ROW_NOCAP = {"time": "time-pre-market", "symbol": "brk/b", "name": "Berkshire", "marketCap": "",
             "epsForecast": "N/A"}


def test_earnings_day_parsed():
    rows = w.parse_earnings_day(day_doc([ROW_PANW, ROW_TJX, ROW_SMALL]), D)
    assert [r["symbol"] for r in rows] == ["PANW", "TJX", "TINY"], rows
    assert rows[0]["session"] == "AMC" and rows[1]["session"] == "BMO" and rows[2]["session"] == "", rows
    assert rows[0]["market_cap"] == 264078852000.0
    assert rows[0]["eps_forecast"] == 0.52
    assert rows[2]["eps_forecast"] == -0.12, rows[2]
    assert rows[0]["eps_actual"] is None  # not reported yet: absent, not 0
    assert rows[0]["date"] == "2026-11-18"
    # The dashboard tile's year-ago comparison reads these (one parser for
    # both surfaces); absent stays None/"" — never 0.
    assert rows[0]["eps_last_year"] == 0.53 and rows[0]["last_year_report"] == "11/19/2025", rows[0]
    assert rows[1]["eps_last_year"] is None and rows[1]["last_year_report"] == "", rows[1]
    assert w.EARN_KIND != "earn"  # rows gained fields: old cached days must not be served


def test_absent_is_none_not_zero():
    rows = w.parse_earnings_day(day_doc([ROW_NOCAP]), D)
    assert rows[0]["market_cap"] is None, rows
    assert rows[0]["eps_forecast"] is None, rows
    assert rows[0]["symbol"] == "BRK.B", rows
    assert w.money("") is None and w.money(None) is None and w.money("N/A") is None
    assert w.money("$0.00") == 0.0  # a real zero stays a zero


def test_no_reports_is_an_answer_refusal_is_not():
    assert w.parse_earnings_day(day_doc(None), D) == []          # Nasdaq: rows null on a quiet day
    assert w.parse_earnings_day(None, D) is None                 # transport failure
    assert w.parse_earnings_day({"message": "denied"}, D) is None
    # An answer for a different day is not this day's answer.
    assert w.parse_earnings_day(day_doc([ROW_PANW], as_of="Thu, Nov 19, 2026"), D) is None


def test_earnings_days_names_what_it_answered():
    """The feed stores only days the answer covers: answered, failed and
    never-asked (past MAX_DAYS) must be told apart."""
    calls = []

    def fake_fetch(cache, kind, url, parse, days, today):
        calls.append(days)
        got = {d: [{"symbol": "X", "date": d.isoformat()}] for d in days[:-1]}
        return got, [days[-1].isoformat()]

    real_fetch, real_cache = w._fetch_days, None
    import ipo_calendar as ic
    real_cache = ic.Cache
    w._fetch_days = fake_fetch
    ic.Cache = lambda path, keep_days=None: None
    try:
        out = w.earnings_days(date(2026, 11, 16), date(2026, 11, 20))
    finally:
        w._fetch_days = real_fetch
        ic.Cache = real_cache
    assert out["days_answered"] == ["2026-11-16", "2026-11-17", "2026-11-18", "2026-11-19"], out
    assert out["days_failed"] == ["2026-11-20"], out
    assert out["days_total"] == 5


def test_fetch_days_calls_the_real_nasdaq_client():
    """_fetch_days through ipo_calendar's REAL _http_json (only the network
    stubbed): a signature change there must fail here, not on every uncached
    day of the earnings feed."""
    import ipo_calendar as ic

    class Resp:
        def raise_for_status(self):
            pass

        def json(self):
            return day_doc([ROW_PANW])

    asked = []

    def fake_get(url, headers=None, timeout=None):
        asked.append(url)
        return Resp()

    real_get = ic.requests.get
    ic.requests.get = fake_get
    try:
        cache = ic.Cache(os.path.join(tempfile.mkdtemp(), "c.sqlite"))
        got, failed = w._fetch_days(cache, w.EARN_KIND, w.EARN_URL, w.parse_earnings_day, [D], D)
    finally:
        ic.requests.get = real_get
    assert asked == [w.EARN_URL.format(D.isoformat())], asked
    assert not failed and [r["symbol"] for r in got[D]] == ["PANW"], (got, failed)


def test_dividends_day():
    doc = {"data": {"calendar": {"asOf": "Mon, Nov 16, 2026", "rows": [
        {"companyName": "Apple Inc.", "symbol": "AAPL", "dividend_Ex_Date": "11/16/2026",
         "payment_Date": "12/04/2026", "dividend_Rate": 0.27},
        {"companyName": "Bad", "symbol": "BAD", "dividend_Ex_Date": "", "dividend_Rate": 0.1},
    ]}}}
    rows = w.parse_dividends_day(doc, date(2026, 11, 16))
    assert rows == [{"symbol": "AAPL", "name": "Apple Inc.", "ex_date": "2026-11-16", "pay_date": "2026-12-04",
                     "amount": 0.27}], rows
    assert w.parse_dividends_day({"data": {}}, date(2026, 11, 16)) is None


def test_window_and_ttl():
    days = w.weekdays(date(2026, 11, 13), date(2026, 11, 17))
    assert days == [date(2026, 11, 13), date(2026, 11, 16), date(2026, 11, 17)], days
    today = date(2026, 11, 13)
    assert w.day_ttl(date(2026, 11, 10), today) == 7 * 86400
    assert w.day_ttl(date(2026, 11, 12), today) == 4 * 3600  # yesterday: late reports still landing
    assert w.day_ttl(date(2026, 11, 20), today) == 4 * 3600
    # A copy fetched before the day closed has no reported EPS: short TTL.
    assert w.day_ttl(date(2026, 11, 10), today, date(2026, 11, 9)) == 4 * 3600
    assert w.day_ttl(date(2026, 11, 10), today, date(2026, 11, 11)) == 4 * 3600
    assert w.day_ttl(date(2026, 11, 10), today, date(2026, 11, 12)) == 7 * 86400


def test_fred_csv_recognised_by_header_only():
    good = "observation_date,CPIAUCSL\n2026-09-01,336.298\n2026-10-01,337.476\n"
    assert w.is_fred_csv(good, "CPIAUCSL")
    assert not w.is_fred_csv(good, "PAYEMS")          # another series' file
    assert not w.is_fred_csv("<!DOCTYPE html><title>Error</title>", "CPIAUCSL")
    assert not w.is_fred_csv("", "CPIAUCSL")
    # Bad ids are refused before any request.
    assert w.fred(["../etc", "a b"], date(2024, 9, 1))["failed"] == ["../etc", "a b"]


def test_insider_clusters_from_local_store():
    with tempfile.TemporaryDirectory() as tmp:
        os.environ["FINTERM_DATA_DIR"] = tmp
        try:
            assert w.insider_clusters(set()) == ([], None)
            import sec_form4_market as f4
            con = f4.connect()
            today = date.today().isoformat()
            for who in ("A", "B"):
                con.execute("INSERT INTO tx (accession, filed, tx_date, symbol, issuer, insider, insider_cik, roles,"
                            " code, direction, shares, price, value, open_market, derivative, ten_pct, plan)"
                            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                            ("acc" + who, today, today, "KO", "Coca-Cola", who, who, "Director", "P", "acquired",
                             1000, 60.0, 60000.0, 1, 0, 0, 0))
            con.execute("INSERT INTO scanned_days VALUES (?)", (today,))
            con.commit()
            con.close()
            rows, last = w.insider_clusters({"KO", "PEP"})
            assert len(rows) == 1 and rows[0]["symbol"] == "KO" and rows[0]["insiders"] == 2, rows
            assert last == today
        finally:
            os.environ.pop("FINTERM_DATA_DIR", None)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS", name)
            except Exception as e:  # noqa: BLE001
                FAILURES.append(name)
                print("FAIL", name, repr(e))
    sys.exit(1 if FAILURES else 0)
