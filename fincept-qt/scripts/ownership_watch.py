#!/usr/bin/env python3
"""
Ownership where the reader already is: watchlist columns, alerts, a calendar.

The per-stock tab and the OWNERSHIP screen answer "who owns this" and "where
is it happening". This script answers the three follow-on questions from the
same three local stores, so the answers cost milliseconds and no network:

    rows      {"symbols": [...]}         one line per symbol for a watchlist —
                                         13F holders and their change, short
                                         interest ÷ 13F shares, days to cover,
                                         scorable insider buys in 30 days
    alerts    {"symbols": [...], "days"?} open-market insider buys on the
                                         reader's own holdings, newest first
    calendar  {"days"?: 90}              the dated events that move these
                                         numbers: 13F deadlines, FINRA
                                         settlement and publication dates, IPO
                                         lock-up expiries

Everything is stated with its date, as on the screens. A symbol the stores
have never seen gets an empty row, never a zero.
"""

import json
import os
import sys
from datetime import date, datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import sec_13f_bulk  # noqa: E402
import finra_short_interest as finra  # noqa: E402
import sec_form4_market as form4  # noqa: E402

try:
    import requests
except ImportError:
    requests = None

NASDAQ_IPO = "https://api.nasdaq.com/api/ipo/calendar?date={}"
NASDAQ_HEADERS = {"User-Agent": "Mozilla/5.0 (X11; Linux x86_64) FinceptTerminal",
                  "Accept": "application/json, text/plain, */*", "Accept-Language": "en-US,en;q=0.9"}
LOCKUP_DAYS = 180
FORM13F_LAG_DAYS = 45


# ── Watchlist rows ──────────────────────────────────────────────────────────

def _thirteen_f(symbols):
    """{symbol: {holders, delta_holders, quarter, prior_quarter, top10_share}}"""
    out = {}
    con = sec_13f_bulk.connect()
    try:
        qs = sec_13f_bulk.quarter_pair(con)
        if not qs:
            return out, None
        cur = qs[0]
        prior = qs[1] if len(qs) > 1 else None
        sec_13f_bulk._ensure_totals(con, cur)
        if prior:
            sec_13f_bulk._ensure_breadth(con, cur, prior)
        for sym in symbols:
            rows = con.execute(
                "SELECT t.cusip, t.holders, t.shares, t.top10_share, b.holders_prior "
                "FROM ticker_totals t LEFT JOIN breadth b ON b.cusip=t.cusip AND b.quarter=t.quarter "
                "AND b.prior_quarter=? WHERE t.quarter=? AND t.ticker=?",
                (prior or "", cur, sym)).fetchall()
            if not rows:
                continue
            holders = sum(r[1] or 0 for r in rows)
            shares = sum(r[2] or 0 for r in rows)
            prior_h = sum(r[4] or 0 for r in rows) if prior else None
            out[sym] = {"holders": holders, "shares": shares,
                        "top10_share": max((r[3] or 0.0) for r in rows) if rows else None,
                        "delta_holders": (holders - prior_h) if prior_h is not None else None,
                        "quarter": cur, "prior_quarter": prior}
        return out, cur
    finally:
        con.close()


def _short(symbols):
    """Latest complete FINRA settlement in the local store; the ranking pages
    one in when there is none yet (a few seconds, once per settlement)."""
    con = finra.connect()
    try:
        row = con.execute("SELECT settlement FROM complete_dates ORDER BY settlement DESC LIMIT 1").fetchone()
        settlement = row[0] if row else None
        if not settlement or (date.today() - date.fromisoformat(settlement)).days > 21:
            r = finra.rank(limit=1)   # pages the newest date into the store
            settlement = r.get("settlement", settlement)
        if not settlement:
            return {}, None
        out = {}
        for sym in symbols:
            r = con.execute("SELECT short, prior, dtc FROM si WHERE settlement=? AND symbol=?",
                            (settlement, sym)).fetchone()
            if r:
                out[sym] = {"short": r[0], "prior": r[1], "dtc": r[2],
                            "change_pct": ((r[0] - r[1]) / r[1]) if (r[0] is not None and r[1]) else None}
        return out, settlement
    finally:
        con.close()


def _insider_buys(symbols, days=30):
    """{symbol: {buys, insiders, value, last_trade}} — scorable buys only."""
    con = form4.connect()
    try:
        cutoff = (date.today() - timedelta(days=int(days))).isoformat()
        out = {}
        for sym in symbols:
            r = con.execute("""
                SELECT COUNT(*), COUNT(DISTINCT COALESCE(NULLIF(insider_cik,''), insider)),
                       SUM(COALESCE(value,0)), MAX(tx_date)
                  FROM tx WHERE symbol=? AND open_market=1 AND derivative=0 AND code='P'
                   AND direction='acquired' AND filed>=?
                   AND COALESCE(ten_pct,0)=0 AND COALESCE(plan,0)=0""", (sym, cutoff)).fetchone()
            if r and r[0]:
                out[sym] = {"buys": r[0], "insiders": r[1], "value": r[2] or 0.0, "last_trade": r[3] or ""}
        scanned = con.execute("SELECT MAX(filed) FROM scanned_days").fetchone()[0]
        return out, scanned
    finally:
        con.close()


def rows(symbols, days=30):
    syms = sorted({str(s).upper().strip() for s in symbols if s})
    if not syms:
        return {"rows": {}}
    thirteen, quarter = _thirteen_f(syms)
    short, settlement = _short(syms)
    buys, scanned = _insider_buys(syms, days)
    out = {}
    for s in syms:
        rec = {}
        t = thirteen.get(s)
        if t:
            rec.update({"holders": t["holders"], "delta_holders": t["delta_holders"],
                        "top10_share": t["top10_share"]})
        si = short.get(s)
        if si:
            rec.update({"short": si["short"], "dtc": si["dtc"], "si_change_pct": si["change_pct"]})
            if t and t["shares"]:
                rec["sirio"] = si["short"] / t["shares"] if si["short"] is not None else None
        b = buys.get(s)
        if b:
            rec.update({"insider_buys": b["buys"], "insider_buyers": b["insiders"],
                        "insider_buy_value": b["value"], "last_insider_buy": b["last_trade"]})
        out[s] = rec
    return {"rows": out, "quarter": quarter, "settlement": settlement,
            "published_after": finra.published_after(settlement) if settlement else "",
            "form4_scanned_to": scanned or "", "days": int(days)}


# ── Alerts ──────────────────────────────────────────────────────────────────

def alerts(symbols, days=7):
    syms = sorted({str(s).upper().strip() for s in symbols if s})
    if not syms:
        return {"rows": []}
    r = form4.recent(days=int(days), min_insiders=1, min_value=0.0, exclude_ten_pct=True,
                     exclude_plan=True, limit=500, symbols=syms)
    return r


# ── Calendar ────────────────────────────────────────────────────────────────

def _is_bday(d):
    return d.weekday() < 5


def _add_bdays(d, n):
    while n > 0:
        d += timedelta(days=1)
        if _is_bday(d):
            n -= 1
    return d


def _last_bday_of_month(y, m):
    d = (date(y + (m // 12), m % 12 + 1, 1) - timedelta(days=1))
    while not _is_bday(d):
        d -= timedelta(days=1)
    return d


def _bday_on_or_before(d):
    while not _is_bday(d):
        d -= timedelta(days=1)
    return d


def finra_settlements(today, days_ahead):
    """FINRA settlement dates in the window: the 15th and the last business
    day of each month (each moved back to a business day), with the
    publication date about ten business days later."""
    out = []
    # Start a month back: a settlement already past can still have its
    # publication ahead, and that is the date the reader is waiting for.
    prev = today.replace(day=1) - timedelta(days=1)
    y, m = prev.year, prev.month
    for _ in range(0, (days_ahead // 28) + 4):
        for d in (_bday_on_or_before(date(y, m, 15)), _last_bday_of_month(y, m)):
            pub = _add_bdays(d, finra.PUBLISH_LAG_BDAYS)
            if today <= pub <= today + timedelta(days=days_ahead):
                out.append({"settlement": d.isoformat(), "published": pub.isoformat()})
        m += 1
        if m > 12:
            m, y = 1, y + 1
    return out


def form13f_deadlines(today, days_ahead):
    """Quarter ends whose 45-day filing deadline falls in the window."""
    out = []
    for y in (today.year - 1, today.year, today.year + 1):
        for qm in (3, 6, 9, 12):
            qe = date(y + (qm // 12), qm % 12 + 1, 1) - timedelta(days=1)
            due = qe + timedelta(days=FORM13F_LAG_DAYS)
            while not _is_bday(due):
                due += timedelta(days=1)
            if today <= due <= today + timedelta(days=days_ahead):
                out.append({"quarter_end": qe.isoformat(), "due": due.isoformat()})
    return out


def lockup_expiries(today, days_ahead, fetch=None):
    """Priced IPOs whose customary 180-day lock-up ends in the window.
    From Nasdaq's public IPO calendar; the 180 days is the convention, not a
    per-deal fact, and the row says so."""
    fetch = fetch or _nasdaq_month
    out = []
    seen = set()
    # An IPO priced between (today − 180d) and (today + window − 180d) expires
    # inside the window; walk those months of the calendar, oldest first.
    earliest = today - timedelta(days=LOCKUP_DAYS)
    latest = today + timedelta(days=days_ahead) - timedelta(days=LOCKUP_DAYS)
    y, m = earliest.year, earliest.month
    while (y, m) <= (latest.year, latest.month):
        for e in fetch(f"{y:04d}-{m:02d}") or []:
            try:
                priced = datetime.strptime(e.get("pricedDate", ""), "%m/%d/%Y").date()
            except ValueError:
                continue
            sym = (e.get("proposedTickerSymbol") or "").strip().upper()
            if not sym or sym in seen:
                continue
            # A SPAC's lock-up runs to its business combination, not 180 days
            # from pricing; the convention does not apply and the row would
            # be a date nobody is waiting for.
            if "acquisition" in (e.get("companyName") or "").lower():
                continue
            expiry = priced + timedelta(days=LOCKUP_DAYS)
            if today <= expiry <= today + timedelta(days=days_ahead):
                seen.add(sym)
                out.append({"symbol": sym, "company": e.get("companyName", ""),
                            "priced": priced.isoformat(), "expiry": expiry.isoformat()})
        m += 1
        if m > 12:
            m, y = 1, y + 1
    out.sort(key=lambda r: r["expiry"])
    return out


def _nasdaq_month(yyyymm):
    if requests is None:
        return []
    try:
        r = requests.get(NASDAQ_IPO.format(yyyymm), headers=NASDAQ_HEADERS, timeout=20)
        r.raise_for_status()
        return r.json().get("data", {}).get("priced", {}).get("rows", []) or []
    except Exception:
        return []


def calendar(days=90, today=None):
    today = today or date.today()
    days = int(days)
    return {"as_of": today.isoformat(), "days": days,
            "form13f": form13f_deadlines(today, days),
            "finra": finra_settlements(today, days),
            "lockups": lockup_expiries(today, days)}


def handle_action(action, p):
    if action == "rows":
        return rows(p.get("symbols") or [], int(p.get("days") or 30))
    if action == "alerts":
        return alerts(p.get("symbols") or [], int(p.get("days") or 7))
    if action == "calendar":
        return calendar(int(p.get("days") or 90))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "calendar"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
