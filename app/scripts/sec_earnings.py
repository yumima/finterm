"""Next week's earnings from the companies' own filings.

The calendar the app reads (Nasdaq's) may not be redistributed and the weekly
brief is public: it only says which companies to look at. What is printed is
SEC's — the company's name and the day it reported the same quarter a year
ago (8-K, Item 2.02) — and only when that day agrees: a company that reported
in the same fortnight last year.
"""

from datetime import date, timedelta

CACHE_KEY = "secearn1:{}"
CACHE_S = 20 * 3600
YEAR = timedelta(days=364)      # the same weekday
NEAR = timedelta(days=14)
MIN_CAP = 10e9
LOOK_AT = 14                    # companies asked about, largest first


def reports(cik, cache):
    """(SEC's name, [days an Item 2.02 8-K was filed], newest first), or None
    when SEC did not answer."""
    import sec_http

    key = CACHE_KEY.format(cik)
    hit = cache.get(key, CACHE_S)
    if hit is None:
        r, _ = sec_http.get(f"https://data.sec.gov/submissions/CIK{int(cik):010d}.json", timeout=25)
        try:
            doc = r.json() if r is not None else None
        except ValueError:
            doc = None
        if not isinstance(doc, dict):
            return None
        rec = (doc.get("filings") or {}).get("recent") or {}
        hit = [doc.get("name") or "", [d for f, d, items in zip(rec.get("form") or [], rec.get("filingDate") or [],
                                                                rec.get("items") or [])
                                       if f == "8-K" and "2.02" in (items or "").split(",")]]
        cache.put(key, hit)
    return hit[0], [date.fromisoformat(d) for d in hit[1]]


def ahead(rows, lo, hi, cache, cik_of, limit=8):
    """([{symbol, company, year_ago}], how many were asked about and not
    confirmed) for the calendar's @rows between @lo and @hi, largest first."""
    big = sorted((r for r in rows if (r.get("market_cap") or 0) >= MIN_CAP and lo.isoformat() <= r["date"] <= hi.isoformat()),
                 key=lambda r: -r["market_cap"])[:LOOK_AT]
    out, unsure = [], 0
    for r in big:
        if len(out) == limit:
            break
        cik = cik_of(r["symbol"]) or cik_of(r["symbol"].replace(".", "-").replace("/", "-"))
        got = reports(cik, cache) if cik else None
        then = date.fromisoformat(r["date"]) - YEAR
        near = [d for d in (got[1] if got else []) if abs(d - then) <= NEAR]
        if not got or not got[0] or not near:
            unsure += 1
            continue
        out.append({"symbol": r["symbol"], "company": got[0], "year_ago": min(near, key=lambda d: abs(d - then))})
    return out, unsure
