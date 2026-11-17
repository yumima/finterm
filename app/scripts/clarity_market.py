"""
Private-market prices for the Private Market screen, from Clarity's public pages.

Clarity (formerly Hiive, a FINRA-registered secondary marketplace) publishes,
logged out, for each company it covers:
  - the "Clarity Price": a daily MODEL price per share built from indications
    and transactions on its platform — an estimate, not a trade print;
  - its funding history: round, month, amount raised, post-money valuation,
    price per share, investors (tender offers included);
and the Clarity Liquidity Index (C50): an equal-weight price index of the 50
most liquid names on the platform, with its members.

Only the public HTML pages are read (the data rides in the page's React
Server Components payload, `self.__next_f.push([1, "..."])`). Requests are
spaced a second apart and identify the project, and every answer is cached so
a day's use is one read per page. The user chose this source (2026-11-17)
knowing Clarity's terms ask for no automated access.

Money in the payload is a display string — "$$73.70", "$$159B" (RSC escapes a
leading "$" as "$$") — and an absent value is "—". Absent stays None: a
missing price or valuation is never 0.

Actions:
  index      — C50 level + history + members, page-1 browse rows, and the
               sitemap's company slugs (the searchable universe).
  companies  — '{"slugs": [...], "max_age_h": 12}' → one normalized record per
               slug (price history, funding rounds, facts).
"""

import json
import os
import re
import sqlite3
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from finterm_identity import project_user_agent  # noqa: E402
from finterm_paths import market_data_path  # noqa: E402

try:
    import requests
except ImportError:  # pragma: no cover
    requests = None

# Bump when what a cached record MEANS changes (the cache stores the parsed
# record, so a parser fix must not be masked by yesterday's parse).
SCHEMA_V = 2  # 2: ipo_date / acquired_at as ISO dates

BASE = "https://www.clarity.com"
COMPANY_URL = BASE + "/browse-companies/{}-stock"
INDEX_URL = BASE + "/clarity-liquidity-index"
BROWSE_URL = BASE + "/browse-companies"
SITEMAP_URL = BASE + "/sitemap.xml"

INDEX_TTL_S = 6 * 3600
SITEMAP_TTL_S = 7 * 86400
GAP_S = 1.0
MAX_SLUGS = 200


# ── parsing (pure) ────────────────────────────────────────────────────────────

_PUSH = re.compile(r'self\.__next_f\.push\(\[1,"((?:[^"\\]|\\.)*)"\]\)')


def flight(html):
    """The page's RSC payload as one string (chunks are JSON string literals)."""
    out = []
    for c in _PUSH.findall(html or ""):
        try:
            out.append(json.loads('"' + c + '"'))
        except ValueError:
            continue
    return "".join(out)


def enclosing_object(s, anchor):
    """The JSON object that contains the first occurrence of `anchor`."""
    i = s.find(anchor)
    if i < 0:
        return None
    depth, j = 0, i
    while j > 0:
        j -= 1
        ch = s[j]
        if ch == "}":
            depth += 1
        elif ch == "{":
            if depth == 0:
                try:
                    obj, _ = json.JSONDecoder().raw_decode(s, j)
                    return obj
                except ValueError:
                    return None
            depth -= 1
    return None


_MULT = {"": 1.0, "K": 1e3, "M": 1e6, "B": 1e9, "T": 1e12}
_MONEY = re.compile(r"^\$*\s*([0-9][0-9,]*(?:\.[0-9]+)?)\s*([KMBT]?)$", re.I)


def parse_money(v):
    """'$$73.70' → 73.7, '$$159B' → 1.59e11, '—' / '' / None → None.
    A Money object {"amount": "91.69"} (browse rows) is accepted too."""
    if isinstance(v, dict):
        v = v.get("amount")
    if v is None:
        return None
    if isinstance(v, (int, float)):
        return float(v)
    m = _MONEY.match(str(v).strip())
    if not m:
        return None
    x = float(m.group(1).replace(",", "")) * _MULT[m.group(2).upper()]
    return x


_PCT = re.compile(r"([+-]?[0-9][0-9,]*(?:\.[0-9]+)?)\s*%")


def parse_pct(v):
    """'+84.25% all time' → 84.25, '-23.63' → -23.63, absent → None."""
    if v is None:
        return None
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v)
    m = _PCT.search(s)
    if m:
        return float(m.group(1).replace(",", ""))
    try:
        return float(s.replace(",", ""))
    except ValueError:
        return None


_MONTHS = {m: i for i, m in enumerate(
    ["january", "february", "march", "april", "may", "june", "july", "august",
     "september", "october", "november", "december"], 1)}


def parse_month(v):
    """'February 2026' → '2026-02' (Clarity dates rounds to the month)."""
    if not v:
        return None
    parts = str(v).strip().split()
    if len(parts) == 2 and parts[0].lower() in _MONTHS and parts[1].isdigit():
        return f"{int(parts[1]):04d}-{_MONTHS[parts[0].lower()]:02d}"
    return None


def parse_long_date(v):
    """'November 17, 2026' → '2026-11-17'."""
    m = re.match(r"^([A-Za-z]+)\s+(\d{1,2}),\s*(\d{4})$", str(v or "").strip())
    if not m or m.group(1).lower() not in _MONTHS:
        return None
    return f"{int(m.group(3)):04d}-{_MONTHS[m.group(1).lower()]:02d}-{int(m.group(2)):02d}"


def _iso_prefix(v):
    """'2026-06-12T00:00:00Z' → '2026-06-12'; anything else → None."""
    m = re.match(r"^(\d{4}-\d{2}-\d{2})", str(v or ""))
    return m.group(1) if m else None


def _text(v):
    return None if v in (None, "", "—") else str(v)


def _investors(v):
    t = _text(v)
    return [x.strip() for x in t.split(",") if x.strip()] if t else []


def normalize_company(o):
    """One company record from the page's company object."""
    price = o.get("price") or {}
    changes = price.get("changes") or {}
    funding = o.get("funding") or {}
    facts = o.get("facts") or {}
    rounds = []
    for r in funding.get("rounds") or []:
        series = _text(r.get("seriesClass"))
        rounds.append({
            "month": parse_month(r.get("closeDate")),
            "series": series,
            "tender": bool(series and "tender" in series.lower()),
            "raised": parse_money(r.get("raised")),
            "post_money": parse_money(r.get("postMoney")),
            "pps": parse_money(r.get("pps")),
            "investors": _investors(r.get("investors")),
        })
    points = []
    for p in (o.get("chart") or {}).get("points") or []:
        v = p.get("value")
        if p.get("day") and isinstance(v, (int, float)) and v > 0:
            points.append([p["day"], float(v)])
    return {
        "slug": o.get("nameSlug"),
        "name": o.get("name"),
        "description": _text(o.get("description")),
        "tags": [t for t in (o.get("heroTags") or []) if t],
        "c50": bool(o.get("isLiquidityIndexMember")),
        "tradeable": bool(o.get("isTradeable")),
        "ipo_date": parse_long_date(o.get("ipoDate")) or _iso_prefix(o.get("ipoDate")),
        "ipo_price": parse_money(o.get("ipoPrice")),
        "public_ticker": _text(o.get("publicTicker")),
        "acquired_at": parse_long_date(o.get("acquiredAt")) or _iso_prefix(o.get("acquiredAt")),
        "price": parse_money(price.get("clarityPrice")),
        "price_date": parse_long_date(o.get("priceDate")),
        "chg_3m": parse_pct((changes.get("threeMonths") or {}).get("label")),
        "chg_6m": parse_pct((changes.get("sixMonths") or {}).get("label")),
        "chg_1y": parse_pct((changes.get("oneYear") or {}).get("label")),
        "chg_max": parse_pct((changes.get("max") or {}).get("label")),
        "points": points,
        "founded": _text(facts.get("founded")),
        "employees": _text(facts.get("employees")),
        "hq": _text(facts.get("headquarters")),
        "website": _text(facts.get("websiteHref")) or _text(facts.get("website")),
        "total_raised": parse_money(funding.get("totalRaised")),
        "key_investors": _investors(funding.get("keyInvestors")),
        "rounds": rounds,
    }


def parse_company_page(html):
    obj = enclosing_object(flight(html), '"price":{"clarityPrice"')
    if not obj or not obj.get("nameSlug"):
        return None
    return normalize_company(obj)


def parse_index_page(html):
    obj = enclosing_object(flight(html), '"members":[')
    if not obj or "index" not in obj:
        return None
    idx = obj.get("index") or {}
    ch = idx.get("changes") or {}
    members = []
    for m in obj.get("members") or []:
        members.append({
            "rank": m.get("rank"),
            "rank_change": m.get("rankChange") if m.get("rankChangeDirection") != "DOWN"
            else -abs(m.get("rankChange") or 0),
            "name": m.get("name"),
            "slug": m.get("nameSlug"),
            "description": _text(m.get("description")),
            "price": parse_money(m.get("clarityPrice")),
            "chg_6m": parse_pct(m.get("sixMonthReturn")),
            "last_round_valuation": parse_money(m.get("lastRoundValuation")),
            "sparkline": [x for x in (m.get("sparkline") or []) if isinstance(x, (int, float))],
        })
    points = [[p["day"], float(p["value"])] for p in idx.get("points") or []
              if p.get("day") and isinstance(p.get("value"), (int, float))]
    return {
        "level": parse_money(idx.get("level")),
        "updated": parse_long_date(idx.get("updatedOn")),
        "chg_3m": parse_pct((ch.get("threeMonths") or {}).get("label")),
        "chg_6m": parse_pct((ch.get("sixMonths") or {}).get("label")),
        "chg_1y": parse_pct((ch.get("oneYear") or {}).get("label")),
        "chg_max": parse_pct((ch.get("max") or {}).get("label")),
        "points": points,
        "members": members,
    }


def parse_browse_page(html):
    obj = enclosing_object(flight(html), '"CompanySearchResultOffsetPage"')
    rows = []
    for it in (obj or {}).get("items") or []:
        rows.append({
            "name": it.get("name"),
            "slug": it.get("nameSlug"),
            "description": _text(it.get("description")),
            "price": parse_money(it.get("clarityPrice")),
            "chg_6m": parse_pct(it.get("sixMonthReturn")),
            "last_round_valuation": parse_money(it.get("lastRoundValuation")),
        })
    total = ((obj or {}).get("pageInfo") or {}).get("totalCount")
    return rows, total


_SLUG = re.compile(r"<loc>https://www\.clarity\.com/browse-companies/([a-z0-9-]+)-stock</loc>")


def parse_sitemap(xml):
    seen, out = set(), []
    for s in _SLUG.findall(xml or ""):
        if s not in seen:
            seen.add(s)
            out.append(s)
    return out


# ── network + cache ───────────────────────────────────────────────────────────

class Cache:
    def __init__(self, path=None):
        self.con = sqlite3.connect(path or market_data_path("clarity_market.sqlite"), timeout=10)
        self.con.execute("CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT, at REAL)")

    def get(self, key, max_age_s):
        row = self.con.execute("SELECT v, at FROM kv WHERE k=?", (f"v{SCHEMA_V}:{key}",)).fetchone()
        if not row or (max_age_s is not None and time.time() - row[1] > max_age_s):
            return None
        return json.loads(row[0])

    def stale(self, key):
        row = self.con.execute("SELECT v, at FROM kv WHERE k=?", (f"v{SCHEMA_V}:{key}",)).fetchone()
        return (json.loads(row[0]), row[1]) if row else (None, None)

    def put(self, key, value):
        self.con.execute("INSERT OR REPLACE INTO kv VALUES (?,?,?)",
                         (f"v{SCHEMA_V}:{key}", json.dumps(value), time.time()))
        self.con.commit()


_gate = threading.Lock()
_last = [0.0]


def _get(url):
    """Body text, or None (a failed read is not an answer — callers keep stale)."""
    if requests is None:
        return None
    with _gate:
        wait = GAP_S - (time.time() - _last[0])
        if wait > 0:
            time.sleep(wait)
        _last[0] = time.time()
    try:
        r = requests.get(url, headers={"User-Agent": project_user_agent(), "Accept": "text/html"},
                         timeout=25)
        if r.status_code == 404:
            return ""
        r.raise_for_status()
        return r.text
    except Exception as e:
        print(f"clarity_market: fetch failed: {url} ({type(e).__name__})", file=sys.stderr)
        return None


def _cached(cache, key, ttl, fetch, parse):
    """(value, fetched_at, from_stale). Fresh cache → network → stale copy."""
    hit = cache.get(key, ttl)
    if hit is not None:
        return hit, cache.stale(key)[1], False
    body = fetch()
    parsed = parse(body) if body else None
    if parsed:
        cache.put(key, parsed)
        return parsed, time.time(), False
    old, at = cache.stale(key)
    return old, at, old is not None


def build_index(cache, max_age_s=INDEX_TTL_S):
    errors = []
    idx, idx_at, idx_stale = _cached(cache, "index", max_age_s, lambda: _get(INDEX_URL), parse_index_page)
    if idx is None:
        errors.append("index unavailable")
    elif idx_stale:
        errors.append("index: showing the last copy (Clarity did not answer)")
    def browse_parse(h):
        rows, total = parse_browse_page(h)
        return {"rows": rows, "total": total} if rows else None

    browse, _, _ = _cached(cache, "browse", max_age_s, lambda: _get(BROWSE_URL), browse_parse)
    slugs, _, _ = _cached(cache, "sitemap", SITEMAP_TTL_S, lambda: _get(SITEMAP_URL),
                          lambda x: parse_sitemap(x) or None)
    return {
        "source": "Clarity",
        "fetched_at": idx_at,
        "index": idx,
        "browse": (browse or {}).get("rows") or [],
        "universe_total": (browse or {}).get("total"),
        "slugs": slugs or [],
        "errors": errors,
    }


def build_companies(cache, slugs, max_age_h=12):
    """errors[slug] is "not_found" when the site answered 404 (no such page —
    final) and "fetch_failed" when it did not answer (timeout, 403, 429 —
    worth asking again later). The caller treats the two differently."""
    out, errors = {}, {}
    ttl = max(1, float(max_age_h)) * 3600
    for slug in slugs[:MAX_SLUGS]:
        if not re.fullmatch(r"[a-z0-9-]{1,120}", slug or ""):
            errors[slug] = "not_found"
            continue
        seen = {}

        def fetch(s=slug):
            seen["body"] = _get(COMPANY_URL.format(s))
            return seen["body"]

        rec, at, stale = _cached(cache, "co:" + slug, ttl, fetch, parse_company_page)
        if rec is None:
            # "" = 404; None = no answer; a page that parsed to nothing is
            # not a company page either.
            errors[slug] = "fetch_failed" if seen.get("body", "") is None else "not_found"
            continue
        rec = dict(rec)
        rec["fetched_at"] = at
        rec["stale"] = stale
        out[slug] = rec
    return {"source": "Clarity", "companies": out, "errors": errors}


def main():
    action = sys.argv[1] if len(sys.argv) > 1 else "index"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    cache = Cache(payload.get("cache_path"))
    if action == "index":
        max_age_s = max(300.0, float(payload.get("max_age_h", INDEX_TTL_S / 3600)) * 3600)
        print(json.dumps(build_index(cache, max_age_s)))
    elif action == "companies":
        print(json.dumps(build_companies(cache, list(payload.get("slugs") or []),
                                         payload.get("max_age_h", 12))))
    else:
        print(json.dumps({"error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
