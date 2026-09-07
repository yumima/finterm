#!/usr/bin/env python3
"""
Twelve months of Form 4, and what the stock did afterwards.

WHY THIS EXISTS
    The per-stock tab and the INSIDER BUYS scan flag an insider cluster buy
    on the strength of the literature (Lakonishok & Lee: +4.8%/yr, +7.3% in
    small caps). Before that flag is allowed to become an ALERT on a holding,
    the app should know whether the effect exists on its own universe and its
    own data — the same discipline the earnings-move work went through. This
    script is that check: every open-market insider purchase in the last N
    quarters, the stock's close on the trade date and 1w / 1m / 3m later, the
    benchmark over the same windows, and the answer by category.

WHERE THE HISTORY COMES FROM
    Not from the daily index — a year of that is 125,000 filing fetches. SEC
    publishes the same filings as quarterly Insider Transactions Data Sets
    (SUBMISSION, REPORTINGOWNER, NONDERIV_TRANS as TSV), about 8 MB per
    quarter. Late for a live screen, perfect for a backtest.

WHAT IS COUNTED
    Non-derivative code P (open-market purchase) and code S rows. The
    exclusions the evidence turns on travel as flags, not filters, so the
    report can show what each category did: 10% beneficial owners, 10b5-1
    plan trades (the AFF10B5ONE checkbox), and sells as the wrong-sign check.
    One EVENT is one issuer on one trade date; a cluster is an issuer with two
    or more distinct owners buying inside 30 days.

ACTIONS
    ingest    {"quarters"?: 5}            download and index the newest N data sets
    evaluate  {"months"?: 12, "benchmark"?: "SPY", "refresh_prices"?: false}
    status    {}
"""

import io
import json
import os
import re
import sqlite3
import sys
import time
import zipfile
from datetime import date, datetime, timedelta

try:
    import requests
except ImportError:
    requests = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

UA = {"User-Agent": "FinceptTerminal research@hanlexon.com", "Accept-Encoding": "gzip, deflate"}
INDEX_URL = "https://www.sec.gov/data-research/sec-markets-data/insider-transactions-data-sets"
CLUSTER_WINDOW_DAYS = 30
CLUSTER_MIN_OWNERS = 2
WINDOWS = {"1w": 7, "1m": 30, "3m": 91}


def db_path():
    root = os.environ.get("FINCEPT_DATA_DIR")
    if not root:
        if sys.platform == "darwin":
            root = os.path.expanduser("~/Library/Application Support/com.fincept.terminal")
        elif os.name == "nt":
            root = os.path.join(os.environ.get("LOCALAPPDATA", ""), "com.fincept.terminal")
        else:
            root = os.path.join(os.path.expanduser("~"), ".local", "share", "com.fincept.terminal")
    os.makedirs(root, exist_ok=True)
    return os.path.join(root, "form4_history.sqlite")


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA busy_timeout=15000")
    con.executescript("""
        CREATE TABLE IF NOT EXISTS datasets (name TEXT PRIMARY KEY, rows INTEGER, ingested_at TEXT);
        CREATE TABLE IF NOT EXISTS tx (
            accession TEXT, filed TEXT, trans_date TEXT, symbol TEXT, issuer_cik TEXT,
            owner_cik TEXT, owner_name TEXT, relationship TEXT, ten_pct INTEGER, plan INTEGER,
            code TEXT, acquired INTEGER, shares REAL, price REAL, value REAL, held_after REAL,
            dataset TEXT
        );
        CREATE INDEX IF NOT EXISTS ix_tx_sym_date ON tx(symbol, trans_date);
        CREATE INDEX IF NOT EXISTS ix_tx_dataset ON tx(dataset);
        CREATE TABLE IF NOT EXISTS closes (symbol TEXT, day TEXT, close REAL, PRIMARY KEY (symbol, day));
        CREATE TABLE IF NOT EXISTS price_runs (symbol TEXT PRIMARY KEY, start TEXT, end TEXT, fetched_at TEXT);
    """)
    con.commit()
    return con


def _get(url, timeout=300):
    if requests is None:
        return None
    try:
        r = requests.get(url, headers=UA, timeout=timeout)
        r.raise_for_status()
        return r
    except Exception:
        return None


def _iso(d):
    """'31-JUL-2025' -> '2025-07-31'; already-ISO strings pass through."""
    if not d:
        return ""
    if re.match(r"^\d{4}-\d{2}-\d{2}$", d):
        return d
    try:
        parsed = datetime.strptime(d, "%d-%b-%Y").date()
    except ValueError:
        return ""
    # SEC data carries the occasional two-digit year ("25-JUL-0025"); a date
    # before EDGAR existed is a typo, not a trade.
    return parsed.isoformat() if parsed.year >= 1993 else ""


def available_datasets():
    """[(name, url), ...] newest first, from SEC's index page."""
    r = _get(INDEX_URL, timeout=60)
    if r is None:
        return []
    out = {}
    for m in re.finditer(r'href="([^"]*?/(\d{4}q[1-4])_form345\.zip)"', r.text):
        path, name = m.group(1), m.group(2)
        url = path if path.startswith("http") else "https://www.sec.gov" + path
        out.setdefault(name, url)
    return sorted(out.items(), key=lambda kv: kv[0], reverse=True)


def _read_tsv(z, name):
    with z.open(name) as f:
        head = f.readline().decode("utf-8", "replace").rstrip("\r\n").split("\t")
        for line in f:
            parts = line.decode("utf-8", "replace").rstrip("\r\n").split("\t")
            yield dict(zip(head, parts))


def ingest_dataset(name, url, con=None, data=None):
    """Index one quarterly data set. `data` (bytes) bypasses the download."""
    own = con is None
    con = con or connect()
    try:
        if con.execute("SELECT 1 FROM datasets WHERE name=?", (name,)).fetchone():
            return {"name": name, "skipped": True}
        if data is None:
            r = _get(url)
            if r is None:
                return {"name": name, "error": "download failed"}
            data = r.content
        z = zipfile.ZipFile(io.BytesIO(data))
        subs = {}
        for row in _read_tsv(z, "SUBMISSION.tsv"):
            if row.get("DOCUMENT_TYPE", "").startswith("4"):
                subs[row["ACCESSION_NUMBER"]] = row
        owners = {}
        for row in _read_tsv(z, "REPORTINGOWNER.tsv"):
            acc = row["ACCESSION_NUMBER"]
            if acc not in subs:
                continue
            # First owner names the row (the parser does the same); every
            # owner's relationship feeds the 10% flag.
            o = owners.setdefault(acc, {"cik": row.get("RPTOWNERCIK", ""),
                                        "name": row.get("RPTOWNERNAME", ""),
                                        "rel": row.get("RPTOWNER_RELATIONSHIP", ""), "ten": False})
            if "TenPercentOwner" in row.get("RPTOWNER_RELATIONSHIP", ""):
                o["ten"] = True
        n = 0
        con.execute("DELETE FROM tx WHERE dataset=?", (name,))
        for row in _read_tsv(z, "NONDERIV_TRANS.tsv"):
            acc = row["ACCESSION_NUMBER"]
            sub = subs.get(acc)
            if not sub or row.get("TRANS_CODE") not in ("P", "S"):
                continue
            o = owners.get(acc, {"cik": "", "name": "", "rel": "", "ten": False})
            shares = _num(row.get("TRANS_SHARES"))
            price = _num(row.get("TRANS_PRICEPERSHARE"))
            con.execute(
                "INSERT INTO tx VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (acc, _iso(sub.get("FILING_DATE", "")), _iso(row.get("TRANS_DATE", "")),
                 (sub.get("ISSUERTRADINGSYMBOL") or "").strip().upper(), sub.get("ISSUERCIK", ""),
                 o["cik"], o["name"], o["rel"], 1 if o["ten"] else 0,
                 1 if (sub.get("AFF10B5ONE") or "0").strip() in ("1", "true") else 0,
                 row["TRANS_CODE"], 1 if row.get("TRANS_ACQUIRED_DISP_CD") == "A" else 0,
                 shares, price, (shares * price) if (shares is not None and price is not None) else None,
                 _num(row.get("SHRS_OWND_FOLWNG_TRANS")), name))
            n += 1
        con.execute("INSERT OR REPLACE INTO datasets VALUES (?,?,?)",
                    (name, n, datetime.now().isoformat(timespec="seconds")))
        con.commit()
        return {"name": name, "rows": n}
    finally:
        if own:
            con.close()


def _num(s):
    try:
        return float(s) if s not in (None, "") else None
    except ValueError:
        return None


def ingest(quarters=5):
    sets = available_datasets()
    if not sets:
        return {"error": "could not list SEC insider data sets"}
    con = connect()
    try:
        out = [ingest_dataset(name, url, con) for name, url in sets[:int(quarters)]]
        return {"ingested": out}
    finally:
        con.close()


# ── Prices ──────────────────────────────────────────────────────────────────

def _fetch_closes(con, symbols, start, end, batch=80, progress=None):
    """Daily closes into the store, in batches. Missing symbols stay missing."""
    todo = [s for s in symbols
            if not con.execute("SELECT 1 FROM price_runs WHERE symbol=? AND start<=? AND end>=?",
                               (s, start, end)).fetchone()]
    fetched = 0
    if not todo:
        return {"requested": len(symbols), "fetched": 0, "already_had": len(symbols)}
    # Imported only when there is something to fetch: an evaluation served
    # entirely from the store must not need the market-data stack installed.
    try:
        import yfinance as yf
        import pandas as pd
    except ImportError:
        return {"error": "yfinance not available"}
    for i in range(0, len(todo), batch):
        chunk = todo[i:i + batch]
        try:
            buf = io.StringIO()
            import contextlib
            with contextlib.redirect_stdout(buf):
                data = yf.download(chunk, start=start, end=end, interval="1d", group_by="ticker",
                                   progress=False, threads=True, auto_adjust=True)
        except Exception:
            continue
        for s in chunk:
            try:
                if isinstance(data.columns, pd.MultiIndex):
                    if s in data.columns.get_level_values(0):
                        closes = data[s]["Close"]
                    elif s in data.columns.get_level_values(1):
                        closes = data.xs(s, axis=1, level=1)["Close"]
                    else:
                        closes = data["Close"] if len(chunk) == 1 else None
                else:
                    closes = data["Close"]
            except Exception:
                closes = None
            if closes is None:
                continue
            if isinstance(closes, pd.DataFrame):
                closes = closes.iloc[:, 0] if closes.shape[1] else None
            if closes is None:
                continue
            rows = []
            for idx, val in closes.items():
                if val is None or pd.isna(val):
                    continue
                rows.append((s, pd.Timestamp(idx).strftime("%Y-%m-%d"), float(val)))
            if rows:
                con.executemany("INSERT OR REPLACE INTO closes VALUES (?,?,?)", rows)
                fetched += 1
            con.execute("INSERT OR REPLACE INTO price_runs VALUES (?,?,?,?)",
                        (s, start, end, datetime.now().isoformat(timespec="seconds")))
        con.commit()
        if progress:
            progress(min(i + batch, len(todo)), len(todo))
        time.sleep(0.5)
    return {"requested": len(symbols), "fetched": fetched, "already_had": len(symbols) - len(todo)}


def _close_on_or_before(series, day):
    """series: sorted [(day, close)]. Last close on or before `day`, or None."""
    lo, hi = 0, len(series)
    while lo < hi:
        mid = (lo + hi) // 2
        if series[mid][0] <= day:
            lo = mid + 1
        else:
            hi = mid
    return series[lo - 1][1] if lo > 0 else None


def forward_returns(series, trade_day, today):
    """{window: return} from the close on the trade day; windows not yet
    elapsed are absent, as on the screens."""
    base = _close_on_or_before(series, trade_day)
    if not base or base <= 0:
        return {}
    d0 = date.fromisoformat(trade_day)
    out = {}
    for name, days in WINDOWS.items():
        mark = d0 + timedelta(days=days)
        if mark > today:
            continue
        px = _close_on_or_before(series, mark.isoformat())
        if px and px > 0:
            out[name] = px / base - 1.0
    return out


def _summarise(events, key):
    """Per window: count, mean and median excess return, hit rate."""
    out = {}
    for w in WINDOWS:
        vals = [e[key][w] for e in events if w in e.get(key, {})]
        if not vals:
            continue
        vals.sort()
        n = len(vals)
        out[w] = {"n": n, "mean": sum(vals) / n, "median": vals[n // 2],
                  "hit_rate": sum(1 for v in vals if v > 0) / n}
    return out


def evaluate(months=12, benchmark="SPY", refresh_prices=False, today=None, con=None, progress=None):
    """The check. Events are issuer-days; returns are excess over the
    benchmark from the trade date; categories are the ones the screens use."""
    own = con is None
    con = con or connect()
    try:
        today = today or date.today()
        since = (today - timedelta(days=int(months) * 30 + 5)).isoformat()
        rows = con.execute("""
            SELECT symbol, trans_date, owner_cik, owner_name, ten_pct, plan, code, acquired,
                   shares, price, value
              FROM tx WHERE trans_date >= ? AND trans_date <= ? AND symbol <> '' AND price IS NOT NULL
            """, (since, today.isoformat())).fetchall()
        if not rows:
            return {"error": "no Form 4 rows in the window — run ingest first"}

        # Issuer-day events, by category.
        events = {}
        for sym, day, ocik, oname, ten, plan, code, acq, shares, price, value in rows:
            if code == "P" and acq:
                cat = "ten_pct_buy" if ten else ("plan_buy" if plan else "buy")
            elif code == "S" and not acq:
                cat = "sell"
            else:
                continue
            key = (sym, day, cat)
            e = events.setdefault(key, {"symbol": sym, "day": day, "cat": cat, "owners": set(),
                                        "value": 0.0, "trades": 0})
            e["owners"].add(ocik or oname)
            e["value"] += value or 0.0
            e["trades"] += 1

        # Clusters: scorable buys by the same issuer within the window, two
        # or more distinct owners across the run. Marked on every event in it.
        by_sym = {}
        for e in events.values():
            if e["cat"] == "buy":
                by_sym.setdefault(e["symbol"], []).append(e)
        for sym, evs in by_sym.items():
            evs.sort(key=lambda x: x["day"])
            i = 0
            while i < len(evs):
                j = i
                while (j + 1 < len(evs) and
                       (date.fromisoformat(evs[j + 1]["day"]) - date.fromisoformat(evs[j]["day"])).days
                       <= CLUSTER_WINDOW_DAYS):
                    j += 1
                owners = set()
                for k in range(i, j + 1):
                    owners |= evs[k]["owners"]
                if len(owners) >= CLUSTER_MIN_OWNERS:
                    for k in range(i, j + 1):
                        evs[k]["cluster"] = True
                i = j + 1

        symbols = sorted({e["symbol"] for e in events.values()} | {benchmark})
        end = (today + timedelta(days=1)).isoformat()
        start = (date.fromisoformat(since) - timedelta(days=7)).isoformat()
        if refresh_prices:
            con.execute("DELETE FROM price_runs")
            con.commit()
        pr = _fetch_closes(con, symbols, start, end, progress=progress)
        if pr.get("error"):
            return pr

        series = {}
        for sym, day, close in con.execute(
                "SELECT symbol, day, close FROM closes WHERE symbol IN (%s) ORDER BY symbol, day"
                % ",".join("?" * len(symbols)), symbols):
            series.setdefault(sym, []).append((day, close))
        bench = series.get(benchmark, [])
        if not bench:
            return {"error": f"no prices for benchmark {benchmark}"}

        scored = []
        unpriced = 0
        for e in events.values():
            s = series.get(e["symbol"])
            if not s:
                unpriced += 1
                continue
            raw = forward_returns(s, e["day"], today)
            b = forward_returns(bench, e["day"], today)
            if not raw:
                unpriced += 1
                continue
            e["raw"] = raw
            e["excess"] = {w: raw[w] - b[w] for w in raw if w in b}
            scored.append(e)

        def pick(pred):
            return [e for e in scored if pred(e)]
        buys = pick(lambda e: e["cat"] == "buy")
        clusters = pick(lambda e: e["cat"] == "buy" and e.get("cluster"))
        singles = pick(lambda e: e["cat"] == "buy" and not e.get("cluster"))
        # Size split on trade value: a proxy for the small-cap effect the
        # literature reports, since the store carries no market cap.
        values = sorted(e["value"] for e in buys)
        cut = values[len(values) // 3] if values else 0
        small = pick(lambda e: e["cat"] == "buy" and e["value"] <= cut)
        large = pick(lambda e: e["cat"] == "buy" and e["value"] > cut * 3) if cut else []
        report = {
            "months": int(months), "benchmark": benchmark, "as_of": today.isoformat(),
            "rows": len(rows), "events": len(events), "scored": len(scored), "unpriced": unpriced,
            "prices": pr,
            "categories": {
                "buy": {"events": len(buys), "excess": _summarise(buys, "excess"), "raw": _summarise(buys, "raw")},
                "cluster_buy": {"events": len(clusters), "excess": _summarise(clusters, "excess"),
                                "raw": _summarise(clusters, "raw")},
                "single_buy": {"events": len(singles), "excess": _summarise(singles, "excess")},
                "ten_pct_buy": {"events": len(pick(lambda e: e["cat"] == "ten_pct_buy")),
                                "excess": _summarise(pick(lambda e: e["cat"] == "ten_pct_buy"), "excess")},
                "plan_buy": {"events": len(pick(lambda e: e["cat"] == "plan_buy")),
                             "excess": _summarise(pick(lambda e: e["cat"] == "plan_buy"), "excess")},
                "sell": {"events": len(pick(lambda e: e["cat"] == "sell")),
                         "excess": _summarise(pick(lambda e: e["cat"] == "sell"), "excess")},
                "buy_small_value": {"events": len(small), "value_cut": cut, "excess": _summarise(small, "excess")},
                "buy_large_value": {"events": len(large), "excess": _summarise(large, "excess")},
            },
        }
        return report
    finally:
        if own:
            con.close()


def status():
    con = connect()
    try:
        sets = con.execute("SELECT name, rows FROM datasets ORDER BY name DESC").fetchall()
        n = con.execute("SELECT COUNT(*) FROM tx").fetchone()[0]
        span = con.execute("SELECT MIN(trans_date), MAX(trans_date) FROM tx").fetchone()
        return {"datasets": [{"name": a, "rows": b} for a, b in sets], "rows": n,
                "first": span[0], "last": span[1]}
    finally:
        con.close()


def handle_action(action, p):
    if action == "ingest":
        return ingest(int(p.get("quarters") or 5))
    if action == "evaluate":
        return evaluate(int(p.get("months") or 12), p.get("benchmark") or "SPY",
                        bool(p.get("refresh_prices", False)))
    if action == "status":
        return status()
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
