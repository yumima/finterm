#!/usr/bin/env python3
"""
FINRA consolidated short interest — the settled short position, twice a month.

WHAT THIS IS
    Every FINRA member reports its short positions as of two settlement dates a
    month (the 15th and the last business day). FINRA consolidates them across
    exchanges into one row per symbol per settlement date: shares short, the
    prior figure, average daily volume and days to cover. It is free, keyless,
    and a year deep.

    This is SHORT INTEREST — a position — as opposed to the daily short-sale
    VOLUME in finra_short_volume.py, which counts the sell side of intraday
    trades. Different numbers; this one is the one the literature is about.

WHY IT REPLACES THE VENDOR FIGURE
    yfinance carries one short-interest reading and one prior. FINRA carries
    24 per year, so a trend can be drawn, and it carries the average daily
    volume the days-to-cover figure was computed from — the number that beats
    the plain short ratio on every cut in Hong et al. (2015).

TIMING
    Firms file two business days after settlement; FINRA publishes about eight
    business days after that. A settlement date is therefore ~two weeks old
    when it first appears, and ~four weeks old just before the next one lands.
    `published_after` says so on every reading rather than leaving the age to
    be guessed.

ACTIONS
    history {"symbol", "months"?}     one symbol's readings, newest first
    dates   {}                        settlement dates FINRA currently serves
    rank    {"date"?, "limit"?, "sort"?, "min_short"?}
                                      every symbol on one settlement date,
                                      joined to 13F institutional shares
                                      (SI ÷ 13F shares — the borrow-fee proxy)
"""

import json
import os
import sqlite3
import sys
import time
from datetime import date, datetime, timedelta

try:
    import requests
except ImportError:
    requests = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

API = "https://api.finra.org/data/group/otcMarket/name/consolidatedShortInterest"
PARTITIONS = "https://api.finra.org/partitions/group/otcMarket/name/consolidatedShortInterest"
UA = {"User-Agent": "finterm research@hanlexon.com",
      "Accept": "application/json", "Content-Type": "application/json"}
PAGE = 5000
# Business days from settlement to public dissemination. FINRA's own schedule:
# firms file by 6pm ET two business days after settlement, publication follows
# about eight business days later.
PUBLISH_LAG_BDAYS = 10

_LAST = 0.0
_GAP = 0.25


def db_path():
    root = os.environ.get("FINCEPT_DATA_DIR")
    if not root:
        if sys.platform == "darwin":
            root = os.path.expanduser("~/Library/Application Support/com.fincept.terminal")
        elif os.name == "nt":
            root = os.path.join(os.environ.get("LOCALAPPDATA", ""), "com.fincept.terminal")
        else:
            root = os.path.join(os.path.expanduser("~"), ".local", "share",
                                "com.fincept.terminal")
    os.makedirs(root, exist_ok=True)
    return os.path.join(root, "short_interest.sqlite")


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA busy_timeout=15000")
    con.executescript("""
        CREATE TABLE IF NOT EXISTS si (
            settlement TEXT, symbol TEXT, short REAL, prior REAL, adv REAL,
            dtc REAL, market_class TEXT,
            PRIMARY KEY (settlement, symbol)
        );
        CREATE INDEX IF NOT EXISTS ix_si_symbol ON si(symbol, settlement);
        -- A settlement date whose whole file has been paged in. Per-symbol
        -- history writes rows for one symbol only, and those must not make a
        -- date look complete for the market-wide ranking.
        CREATE TABLE IF NOT EXISTS complete_dates (settlement TEXT PRIMARY KEY, rows INTEGER,
                                                   fetched_at TEXT);
    """)
    con.commit()
    return con


def _post(body, timeout=90):
    """One API call, rate-limited. Returns a list of rows or None on failure."""
    global _LAST
    if requests is None:
        return None
    gap = time.time() - _LAST
    if gap < _GAP:
        time.sleep(_GAP - gap)
    try:
        r = requests.post(API, json=body, headers=UA, timeout=timeout)
        _LAST = time.time()
        if r.status_code != 200:
            return None
        data = r.json()
        return data if isinstance(data, list) else None
    except Exception:
        _LAST = time.time()
        return None


def _row(rec):
    def num(k):
        v = rec.get(k)
        try:
            return float(v) if v is not None else None
        except (TypeError, ValueError):
            return None
    return {
        "settlement": rec.get("settlementDate") or "",
        "symbol": (rec.get("symbolCode") or "").upper(),
        "short": num("currentShortPositionQuantity"),
        "prior": num("previousShortPositionQuantity"),
        "adv": num("averageDailyVolumeQuantity"),
        "dtc": num("daysToCoverQuantity"),
        "market_class": rec.get("marketClassCode") or "",
    }


def _store(con, rows):
    con.executemany(
        "INSERT OR REPLACE INTO si VALUES (?,?,?,?,?,?,?)",
        [(r["settlement"], r["symbol"], r["short"], r["prior"], r["adv"], r["dtc"],
          r["market_class"]) for r in rows if r["settlement"] and r["symbol"]])
    con.commit()


def add_business_days(d, n):
    while n > 0:
        d += timedelta(days=1)
        if d.weekday() < 5:
            n -= 1
    return d


def published_after(settlement):
    try:
        d = datetime.strptime(settlement, "%Y-%m-%d").date()
    except ValueError:
        return ""
    return add_business_days(d, PUBLISH_LAG_BDAYS).isoformat()


def dates():
    """Settlement dates FINRA currently serves, newest first."""
    if requests is None:
        return {"error": "requests not available"}
    try:
        r = requests.get(PARTITIONS, headers=UA, timeout=30)
        r.raise_for_status()
        parts = r.json().get("availablePartitions", [])
    except Exception as e:
        return {"error": f"FINRA partitions: {e}"}
    out = sorted({p["partitions"][0] for p in parts if p.get("partitions")}, reverse=True)
    return {"dates": out}


def history(symbol, months=12):
    """One symbol's readings, newest first, from FINRA with the local store as
    a fallback when the network is out."""
    sym = str(symbol or "").upper().strip()
    if not sym:
        return {"error": "symbol required"}
    since = (date.today() - timedelta(days=int(months) * 31)).isoformat()
    con = connect()
    try:
        fetched = _post({"limit": PAGE, "compareFilters": [
            {"compareType": "EQUAL", "fieldName": "symbolCode", "fieldValue": sym}]})
        source = "finra"
        if fetched is not None:
            _store(con, [_row(r) for r in fetched])
        else:
            source = "local cache"
        rows = con.execute(
            "SELECT settlement, short, prior, adv, dtc FROM si "
            "WHERE symbol=? AND settlement>=? ORDER BY settlement DESC",
            (sym, since)).fetchall()
        if not rows:
            return {"symbol": sym, "error": "no FINRA short-interest rows for this symbol"
                    + ("" if fetched is not None else " (offline)")}
        out = []
        for settlement, short, prior, adv, dtc in rows:
            rec = {"settlement": settlement, "short": short, "prior": prior,
                   "adv": adv, "dtc": dtc, "published_after": published_after(settlement)}
            if short is not None and prior:
                rec["change_pct"] = (short - prior) / prior
            out.append(rec)
        return {"symbol": sym, "source": source, "months": int(months),
                "latest": out[0], "rows": out}
    finally:
        con.close()


def _ensure_date(con, settlement):
    """Page one settlement date's whole file into the store. ~15k rows, 3 pages."""
    have = con.execute("SELECT rows FROM complete_dates WHERE settlement=?",
                       (settlement,)).fetchone()
    if have:
        return have[0], True
    total = 0
    offset = 0
    while True:
        page = _post({"limit": PAGE, "offset": offset,
                      "compareFilters": [{"compareType": "EQUAL", "fieldName": "settlementDate",
                                          "fieldValue": settlement}],
                      "fields": ["symbolCode", "currentShortPositionQuantity",
                                 "previousShortPositionQuantity", "averageDailyVolumeQuantity",
                                 "daysToCoverQuantity", "settlementDate", "marketClassCode"]})
        if page is None:
            return total, False
        _store(con, [_row(r) for r in page])
        total += len(page)
        if len(page) < PAGE:
            break
        offset += PAGE
    con.execute("INSERT OR REPLACE INTO complete_dates VALUES (?,?,?)",
                (settlement, total, datetime.now().isoformat(timespec="seconds")))
    con.commit()
    return total, True


def rank(settlement=None, limit=200, sort="sirio", min_short=100_000.0, min_holders=50,
         include_funds=False):
    """Every symbol on one settlement date, joined to 13F institutional shares.

    SI ÷ 13F shares is Drechsler & Drechsler's proxy for the borrow fee — the
    short-side measure that actually replicates — and it needs the index,
    which is the reason this script knows about form13f.sqlite at all.
    """
    con = connect()
    try:
        if not settlement:
            d = dates()
            if d.get("error"):
                row = con.execute(
                    "SELECT settlement FROM complete_dates ORDER BY settlement DESC LIMIT 1"
                ).fetchone()
                if not row:
                    return d
                settlement = row[0]
            else:
                settlement = d["dates"][0]
        total, complete = _ensure_date(con, settlement)
        if not complete and total == 0:
            return {"error": f"could not fetch FINRA short interest for {settlement}"}

        try:
            import sec_13f_bulk
            totals = sec_13f_bulk.shares_by_ticker()
        except Exception as e:   # no index, or the module is unavailable
            totals = {"error": str(e), "by_ticker": {}}
        by_ticker = totals.get("by_ticker", {})
        quarter = totals.get("quarter")

        rows = con.execute(
            "SELECT symbol, short, prior, adv, dtc, market_class FROM si "
            "WHERE settlement=? AND short>=?", (settlement, float(min_short))).fetchall()
        out = []
        funds_dropped = 0
        for sym, short, prior, adv, dtc, mc in rows:
            inst = by_ticker.get(sym)
            rec = {"symbol": sym, "short": short, "prior": prior, "adv": adv, "dtc": dtc,
                   "market_class": mc}
            if short is not None and prior:
                rec["change_pct"] = (short - prior) / prior
            if inst and inst.get("shares"):
                # Stocks only, by the 13F issuer name. An ETF's short interest
                # is a hedge, and a levered one tops this list by construction.
                if inst.get("fund") and not include_funds:
                    funds_dropped += 1
                    continue
                if (inst.get("holders") or 0) < int(min_holders):
                    continue
                rec["name"] = inst.get("name", "")
                rec["inst_shares"] = inst["shares"]
                rec["holders"] = inst.get("holders", 0)
                rec["sirio"] = short / inst["shares"] if short is not None else None
            out.append(rec)

        if sort == "dtc":
            key = lambda r: (r.get("dtc") or 0.0)
        elif sort == "change":
            key = lambda r: (r.get("change_pct") or 0.0)
        elif sort == "short":
            key = lambda r: (r.get("short") or 0.0)
        else:
            sort = "sirio"
            key = lambda r: (r.get("sirio") or 0.0)
        out.sort(key=key, reverse=True)

        # Percentile of SI/13F across the joined universe, so a per-stock
        # reading can say "top decile" against something measured.
        sirio_sorted = sorted(r["sirio"] for r in out if r.get("sirio") is not None)
        return {"settlement": settlement, "published_after": published_after(settlement),
                "quarter": quarter, "symbols": len(rows),
                "joined": len(sirio_sorted), "complete": complete,
                "funds_dropped": funds_dropped, "min_holders": int(min_holders),
                "sort": sort, "rows": out[:int(limit)],
                "sirio_deciles": [sirio_sorted[int(len(sirio_sorted) * k / 10)]
                                  for k in range(1, 10)] if len(sirio_sorted) >= 10 else []}
    finally:
        con.close()


def sirio_percentile(symbol, settlement=None):
    """Where one symbol's SI ÷ 13F shares sits across the ranked universe."""
    r = rank(settlement, limit=1_000_000, sort="sirio")
    if r.get("error"):
        return r
    sym = str(symbol or "").upper()
    vals = [x["sirio"] for x in r["rows"] if x.get("sirio") is not None]
    mine = next((x["sirio"] for x in r["rows"]
                 if x["symbol"] == sym and x.get("sirio") is not None), None)
    if mine is None or not vals:
        return {"symbol": sym, "settlement": r["settlement"], "percentile": None,
                "universe": len(vals)}
    below = sum(1 for v in vals if v < mine)
    return {"symbol": sym, "settlement": r["settlement"], "sirio": mine,
            "percentile": below / len(vals), "universe": len(vals)}


def handle_action(action, p):
    if action == "history":
        return history(p.get("symbol"), int(p.get("months") or 12))
    if action == "dates":
        return dates()
    if action == "rank":
        return rank(p.get("date"), int(p.get("limit") or 200), p.get("sort") or "sirio",
                    float(p.get("min_short") or 100_000.0), int(p.get("min_holders") or 50),
                    bool(p.get("include_funds", False)))
    if action == "percentile":
        return sirio_percentile(p.get("symbol"), p.get("date"))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "dates"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
