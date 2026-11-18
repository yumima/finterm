"""Which "private" companies have since listed.

    pre_ipo_listings.py listings '{"ciks": ["0001181412", ...]}'

IPO Watch follows companies before they list — Form D filers, a curated
shortlist, S-1 registrants. None of those sources says when a company stops
being private: SpaceX stayed on the pre-IPO list months after it began
trading. SEC's own list of exchange-traded registrants does: a CIK appears in
company_tickers.json once its shares trade, with the ticker. The first trading
day then comes from the quote vendor.

Prints {"listed": {cik: {"ticker", "listed_on" | null}}, "checked": n} or
{"error": str}. A CIK not in the list is simply absent — still private.
"""

import json
import sys
from datetime import datetime, timezone


def pad(cik):
    digits = "".join(ch for ch in str(cik or "") if ch.isdigit())
    return digits.zfill(10) if digits else ""


def listed_from(rows, ciks):
    """{cik: ticker} for the requested CIKs that SEC lists as traded, from
    sec_directory rows. A registrant with several share classes appears once
    per class; the first row is its primary listing."""
    wanted = {pad(c) for c in ciks if pad(c)}
    out = {}
    for row in rows or []:
        cik = pad(row.get("cik"))
        ticker = str(row.get("ticker") or "").strip().upper()
        if cik in wanted and ticker and cik not in out:
            out[cik] = ticker
    return out


def first_trade_dates(tickers):
    """{ticker: ISO date} from Yahoo's quote row; a ticker it does not know is
    left out (its date is then unknown, which is not the same as today)."""
    if not tickers:
        return {}
    from yfinance.data import YfData
    out = {}
    for i in range(0, len(tickers), 50):
        doc = YfData().get_raw_json(
            "https://query1.finance.yahoo.com/v7/finance/quote", timeout=15,
            params={"symbols": ",".join(tickers[i:i + 50]), "formatted": "false",
                    "fields": "firstTradeDateMilliseconds"})
        for q in ((doc or {}).get("quoteResponse") or {}).get("result") or []:
            ms = q.get("firstTradeDateMilliseconds")
            if isinstance(ms, (int, float)) and ms > 0:
                out[str(q.get("symbol")).upper()] = datetime.fromtimestamp(ms / 1000, tz=timezone.utc).date().isoformat()
    return out


def listings(ciks):
    ciks = [c for c in (ciks or []) if pad(c)]
    if not ciks:
        return {"listed": {}, "checked": 0}
    try:
        import sec_directory  # the app's one copy of SEC's directory
        found = listed_from(sec_directory.rows(), ciks)
    except Exception as e:
        return {"error": f"SEC's ticker list did not load ({str(e)[:160]})"}
    try:
        # SEC writes a class share as BRK-B; so does Yahoo.
        dates = first_trade_dates(sorted(set(found.values())))
    except Exception:
        dates = {}  # the listing is still a fact; only its date is unknown
    return {"listed": {cik: {"ticker": t, "listed_on": dates.get(t)} for cik, t in found.items()},
            "checked": len(ciks)}


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else ""
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    if action != "listings":
        print(json.dumps({"error": "usage: pre_ipo_listings.py listings '{\"ciks\": [...]}'"}))
        sys.exit(1)
    print(json.dumps(listings(payload.get("ciks"))))
