#!/usr/bin/env python3
"""Company events for a date window (Nasdaq public calendars).

    whats_on_calendar.py earnings START END
        every company Nasdaq lists as reporting on each weekday in the window
        (no size cut: each surface makes its own — the dashboard tile ranks
        by market cap, What's On keeps large caps and the user's names in
        WhatsOnModel.h). Read by the app's ONE earnings feed
        (services/calendar/EarningsCalendarFeed) for every surface.
        days_failed: weekdays Nasdaq did not answer (and no earlier copy
        exists) — a refusal is NOT "no earnings that day", so the app says so.

    whats_on_calendar.py company_events START END [SYM,SYM,...]
        dividends  ex-dividend dates in the window — listed symbols ONLY (the
                   full calendar is hundreds of rows a day and not news).
        insiders   open-market insider cluster buys (>= 2 insiders) filed in
                   the last INSIDER_DAYS days on listed symbols, read from the
                   Ownership scan's LOCAL store (sec_form4_market) — no
                   network; absent when that scan has never run.
        div_days_failed  weekdays the dividend calendar did not answer.

Source: Nasdaq's public calendar API (api.nasdaq.com/api/calendar/earnings,
/dividends?date=YYYY-MM-DD) — the same host IPO Watch reads, through the
same paced client (ipo_calendar._http_json). Each day is cached in
market_data/whats_on_calendar.sqlite: a closed day for a week, today and later
for 4 h; a failed refresh falls back to the last copy.

    whats_on_calendar.py fred ID[,ID...] START

prints {"series": {ID: "<fredgraph.csv text>"}, "failed": [ID...]} — FRED's
public graph download (no API key; the keyed API is what fred_data.py needs
and the app holds no key). The app derives the printed number itself
(MacroReleases.h). Fetched here rather than from Qt because FRED's edge
resets the HTTP/2 stream for the app's own User-Agent.
"""

import json
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import date, datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

INSIDER_DAYS = 10
MAX_DAYS = 70            # guard against a runaway window

EARN_URL = "https://api.nasdaq.com/api/calendar/earnings?date={}"
DIV_URL = "https://api.nasdaq.com/api/calendar/dividends?date={}"

_SESSION = {"time-pre-market": "BMO", "time-after-hours": "AMC"}
# Cache key prefix for parsed earnings days. Bump when parse_earnings_day's
# row shape changes, or cached days come back without the new fields.
EARN_KIND = "earn2"  # earn2: eps_last_year + last_year_report
EARN_KIND_PREV = "earn"
# Days are read from this week on; a day two months past is never asked for
# again, and older key generations (earn:) age out with it.
CACHE_KEEP_DAYS = 60


# ── pure parsers (tests/scripts/test_whats_on_calendar.py) ───────────────────

def norm_symbol(s):
    """BRK.B / BRK-B / BRK/B → BRK.B; upper case; '' for junk."""
    s = (s or "").strip().upper()
    return re.sub(r"[-/]", ".", s)


def money(s):
    """'$264,078,852,000' → 264078852000.0; '($0.12)' → -0.12; absent → None
    (never 0 — a missing market cap is unknown, not a micro cap)."""
    if s is None:
        return None
    if isinstance(s, (int, float)):
        return float(s)
    t = str(s).strip()
    if not t or t.upper() in ("N/A", "NA", "--", "-"):
        return None
    neg = t.startswith("(") and t.endswith(")")
    t = t.strip("()").replace("$", "").replace(",", "").strip()
    try:
        v = float(t)
    except ValueError:
        return None
    return -v if neg else v


def as_of_date(s):
    """Nasdaq's 'Thu, Nov 12, 2026' → date, or None."""
    try:
        return datetime.strptime((s or "").strip(), "%a, %b %d, %Y").date()
    except ValueError:
        return None


def parse_earnings_day(doc, day):
    """One /calendar/earnings answer → list of rows, or None when the answer is
    not a usable answer for `day` (refusal, wrong day, malformed). A day with
    no reports is [] — an answer, not a failure."""
    if not isinstance(doc, dict) or not isinstance(doc.get("data"), dict):
        return None
    data = doc["data"]
    got = as_of_date(data.get("asOf"))
    if got is not None and got != day:
        return None
    out = []
    for r in data.get("rows") or []:
        if not isinstance(r, dict):
            continue
        sym = norm_symbol(r.get("symbol"))
        if not sym:
            continue
        out.append({
            "symbol": sym,
            "name": (r.get("name") or "").strip(),
            "date": day.isoformat(),
            "session": _SESSION.get(r.get("time") or "", ""),
            "market_cap": money(r.get("marketCap")),
            "eps_forecast": money(r.get("epsForecast")),
            "eps_actual": money(r.get("eps")),
            "surprise_pct": money(r.get("surprise")),
            "fiscal_quarter": (r.get("fiscalQuarterEnding") or "").strip(),
            "estimates": money(r.get("noOfEsts")),
            "eps_last_year": money(r.get("lastYearEPS")),
            "last_year_report": (r.get("lastYearRptDt") or "").strip(),
        })
    return out


def _mdy(s):
    try:
        return datetime.strptime((s or "").strip(), "%m/%d/%Y").date().isoformat()
    except ValueError:
        return ""


def parse_dividends_day(doc, day):
    if not isinstance(doc, dict) or not isinstance(doc.get("data"), dict):
        return None
    cal = doc["data"].get("calendar")
    if not isinstance(cal, dict):
        return None
    got = as_of_date(cal.get("asOf"))
    if got is not None and got != day:
        return None
    out = []
    for r in cal.get("rows") or []:
        if not isinstance(r, dict):
            continue
        sym = norm_symbol(r.get("symbol"))
        ex = _mdy(r.get("dividend_Ex_Date"))
        if not sym or not ex:
            continue
        out.append({
            "symbol": sym,
            "name": (r.get("companyName") or "").strip(),
            "ex_date": ex,
            "pay_date": _mdy(r.get("payment_Date")),
            "amount": money(r.get("dividend_Rate")),
        })
    return out


def weekdays(start, end):
    d, out = start, []
    while d <= end and len(out) < MAX_DAYS:
        if d.weekday() < 5:
            out.append(d)
        d += timedelta(days=1)
    return out


def day_ttl(day, today, written_on=None):
    """A closed day keeps for a week — but only a copy WRITTEN once the day had
    closed (`written_on` >= day + 2, the same boundary). A copy fetched while
    the day was still ahead holds no reported EPS and stays on the short TTL,
    so EARLIER THIS WEEK gets its numbers."""
    closed = day < today - timedelta(days=1)
    if closed and (written_on is None or written_on >= day + timedelta(days=2)):
        return 7 * 86400
    return 4 * 3600


# ── fetch ────────────────────────────────────────────────────────────────────

def _fetch_days(cache, kind, url, parse, days, today):
    """{day: rows} for every day answered (fresh, cached or last copy), and the
    list of days with no answer at all."""
    import ipo_calendar as ic
    got, todo = {}, []
    for d in days:
        key = f"{kind}:{d.isoformat()}"
        at = cache.written_at(key)
        written_on = date.fromtimestamp(at) if at is not None else None
        hit = cache.get(key, day_ttl(d, today, written_on))
        if hit is not None:
            got[d] = hit
        else:
            todo.append(d)

    def one(d):
        return d, parse(ic._http_json("nasdaq", url.format(d.isoformat()), ic.NASDAQ_HEADERS), d)

    failed = []
    if todo:
        with ThreadPoolExecutor(max_workers=ic.WORKERS) as ex:
            results = list(ex.map(one, todo))
        for d, rows in results:  # sqlite stays on this thread
            key = f"{kind}:{d.isoformat()}"
            if rows is None:
                stale = cache.stale(key)
                if stale is None and kind == EARN_KIND:
                    # The last copy under the previous row shape: missing the
                    # newer fields (read as unknown), still the day's reports.
                    stale = cache.stale(f"{EARN_KIND_PREV}:{d.isoformat()}")
                if stale is not None:
                    got[d] = stale
                else:
                    failed.append(d.isoformat())
                continue
            cache.put(key, rows)
            got[d] = rows
    return got, failed


def insider_clusters(mine):
    if not mine:
        return [], None
    try:
        import sec_form4_market as f4
        if not os.path.exists(f4.db_path()):
            return [], None
        r = f4.recent(days=INSIDER_DAYS, min_insiders=2, symbols=sorted(mine), limit=50)
    except Exception as e:  # the local store is optional; never fail the calendar on it
        print(f"whats_on_calendar: insider store unreadable ({type(e).__name__})", file=sys.stderr)
        return [], None
    rows = [{
        "symbol": norm_symbol(x.get("symbol")),
        "issuer": x.get("issuer") or "",
        "insiders": x.get("insiders") or 0,
        "value": x.get("value"),
        "last_filed": x.get("last_filed") or "",
        "last_trade": x.get("last_trade") or "",
    } for x in r.get("rows", []) if x.get("symbol")]
    return rows, r.get("last_scanned") or None


def company_events(start, end, symbols):
    """The user's own names: ex-dividend dates and insider buying clusters.
    (Earnings come from `earnings`, which the app's one earnings feed reads
    for every surface.)"""
    import ipo_calendar as ic
    today = date.today()
    mine = {norm_symbol(s) for s in symbols if norm_symbol(s)}
    days = weekdays(start, end)
    dividends, div_failed = [], []
    if mine:
        cache = ic.Cache(ic.market_data_path("whats_on_calendar.sqlite"), keep_days=CACHE_KEEP_DAYS)
        div, div_failed = _fetch_days(cache, "div", DIV_URL, parse_dividends_day, days, today)
        for d in sorted(div):
            dividends += [r for r in div[d] if r["symbol"] in mine]
    insiders, last_scanned = insider_clusters(mine)
    return {
        "start": start.isoformat(), "end": end.isoformat(),
        "dividends": dividends, "insiders": insiders,
        "insider_last_scanned": last_scanned,
        "days_total": len(days), "div_days_failed": div_failed,
    }


def earnings_days(start, end):
    """Every company reporting on each weekday in [start, end] (at most
    MAX_DAYS weekdays) — no size cut; each surface makes its own.

    days_answered lists the weekdays this answer covers (fresh, cached or the
    last copy); days_failed the ones with no answer at all. A weekday in
    neither (past the MAX_DAYS cap) was not asked: the caller must not read
    it as a quiet day."""
    import ipo_calendar as ic
    today = date.today()
    days = weekdays(start, end)
    cache = ic.Cache(ic.market_data_path("whats_on_calendar.sqlite"), keep_days=CACHE_KEEP_DAYS)
    earn, failed = _fetch_days(cache, EARN_KIND, EARN_URL, parse_earnings_day, days, today)
    rows = []
    for d in sorted(earn):
        rows += earn[d]
    return {"start": start.isoformat(), "end": end.isoformat(), "earnings": rows,
            "days_total": len(days), "days_failed": failed,
            "days_answered": [d.isoformat() for d in sorted(earn)]}


FRED_CSV = "https://fred.stlouisfed.org/graph/fredgraph.csv"
_FRED_ID = re.compile(r"^[A-Z0-9]{2,30}$")


def is_fred_csv(text, series_id):
    """A real download starts with the header naming the series; an error
    page or a throttle notice does not — it is a failure, not an empty series."""
    first = (text or "").lstrip().split("\n", 1)[0].strip()
    return first.lower().startswith("observation_date,") and first.split(",", 1)[1].strip() == series_id


def _fred_one(requests, sid, start):
    """(sid, csv text) or (sid, None) — one refused series must not lose the others."""
    if not _FRED_ID.match(sid):
        return sid, None
    try:
        r = requests.get(FRED_CSV, params={"id": sid, "cosd": start.isoformat()}, timeout=15)
        r.raise_for_status()
        return sid, (r.text if is_fred_csv(r.text, sid) else None)
    except Exception as e:
        print(f"whats_on_calendar: FRED {sid} failed ({type(e).__name__})", file=sys.stderr)
        return sid, None


def fred(ids, start):
    try:
        import requests
    except ImportError:  # pragma: no cover
        return {"series": {}, "failed": list(ids)}
    # Concurrent: the app gives the whole call 60 s, so a few slow series in a
    # row must not run the deadline out and lose the ones that answered.
    out, failed = {}, []
    if ids:
        with ThreadPoolExecutor(max_workers=min(6, len(ids))) as ex:
            for sid, text in ex.map(lambda s: _fred_one(requests, s, start), ids):
                if text is None:
                    failed.append(sid)
                else:
                    out[sid] = text
    return {"series": out, "failed": failed}


def main(argv):
    if len(argv) >= 3 and argv[0] == "fred":
        try:
            start = date.fromisoformat(argv[2])
        except ValueError:
            print(json.dumps({"error": "bad date"}))
            return 2
        ids = [s.strip().upper() for s in argv[1].split(",") if s.strip()]
        print(json.dumps(fred(ids, start)))
        return 0
    if len(argv) < 3 or argv[0] not in ("earnings", "company_events"):
        print(json.dumps({"error": "usage: whats_on_calendar.py earnings|company_events START END [SYM,...]"}))
        return 2
    try:
        start = date.fromisoformat(argv[1])
        end = date.fromisoformat(argv[2])
    except ValueError:
        print(json.dumps({"error": "bad date"}))
        return 2
    if argv[0] == "earnings":
        out = earnings_days(start, end)
        if out["days_total"] and len(out["days_failed"]) == out["days_total"]:
            out["error"] = "Nasdaq earnings calendar did not answer"
        print(json.dumps(out))
        return 0
    syms = [s for s in (argv[3].split(",") if len(argv) > 3 else []) if s.strip()]
    print(json.dumps(company_events(start, end, syms)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
