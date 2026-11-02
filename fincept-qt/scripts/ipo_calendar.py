"""
IPO calendar for IPO Watch: one row per DEAL, with a projected pricing date.

Nasdaq's public calendar (api.nasdaq.com/api/ipo/calendar?date=YYYY-MM) files
each deal under the month of whatever happened to it — `filed` rows under the
month of the first public S-1/F-1, `priced` under the pricing month,
`withdrawn` under the withdrawal month, and `upcoming` (the only bucket with an
expected date) for roughly the next week. The same dealID appears in several
of those, so reading the buckets as separate rows shows a deal that priced in
June as a live "filed" pipeline entry from May, and never subtracts the
withdrawals. Future months are always empty.

This script:
  1. reads ~13 months of the calendar and merges the buckets by dealID into
     one lifecycle row: filed → (scheduled) → priced | withdrawn;
  2. for every still-pending deal, reads Nasdaq's deal overview (CIK, price
     range, lock-up term) and the issuer's EDGAR submissions, and places the
     deal on the registration ladder:
        S-1/F-1 on file → amendments → 8-A12B (exchange registration)
        → CERT (exchange certifies the listing) → EFFECT (SEC declares the
        registration effective) → 424B4 (final prospectus = priced)
     plus RW (withdrawn);
  3. projects the pricing date — and says what the projection rests on:
        scheduled — Nasdaq's expectedPriceDate (the underwriters' date)
        imminent  — an 8-A / CERT / EFFECT is on file; window measured below
        estimate  — nothing imminent; the window is the conditional pricing
                    lag of comparable filers (same class, still unresolved at
                    the same age) from this same 13-month history, and the
                    share of them that priced at all is reported with it.
                    Backtest 2026-11-01 at four as-of dates (May–Aug 2026,
                    future events masked): the q25–q75 window held 45–65% of
                    the deals that went on to price (misses mostly EARLY),
                    median miss ~2 weeks; the share was calibrated (predicted
                    0–20% → 12% priced, 20–40% → 40%, 60–80% → 60%).
        long_shot — under MIN_SHARE of comparables priced: no date
        overdue / stalled — past its window, not priced (≤ / > 21 days)
        dormant   — no registration activity for DORMANT_DAYS
  4. lists lock-up expiries of recent priced deals from each deal's own
     lock-up term (Nasdaq's overview, taken from the prospectus); 180 days is
     used only where Nasdaq has none, and the row says so.

Actions:
  calendar — merged deals + comparables stats (Nasdaq only, fast).
  enrich   — calendar + EDGAR stage + projection for pending deals, lock-ups.
             Cold: ~2–3 minutes (one Nasdaq overview + one SEC submissions
             read per pending deal); warm: seconds.
"""

import json
import os
import re
import sqlite3
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import date, datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from finterm_identity import user_agent  # noqa: E402  (SEC only — see finterm_identity)
from finterm_paths import market_data_path  # noqa: E402

try:
    import requests
except ImportError:  # pragma: no cover
    requests = None

# Bump when what a cached row MEANS changes (a cache key must encode the
# computation, not just its inputs).
SCHEMA_V = 1

NASDAQ_CAL = "https://api.nasdaq.com/api/ipo/calendar?date={}"
NASDAQ_DEAL = "https://api.nasdaq.com/api/ipo/overview/?dealId={}"
NASDAQ_HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                  "Chrome/120 Safari/537.36 finterm",
    "Accept": "application/json",
}
SEC_SUBMISSIONS = "https://data.sec.gov/submissions/CIK{}.json"

MONTHS_BACK = 13          # history window: comparables + lock-ups need a year
DORMANT_DAYS = 120        # no registration filing for this long → dormant
LOCKUP_DEFAULT = 180      # convention, used only when the deal has no term
MIN_COMPARABLES = 5       # fewer priced comparables → no estimate window
MIN_SHARE = 0.10          # under this share of comparables pricing → no date

# Days from an EDGAR event to pricing (p10…p90), measured 2026-11-01 on the
# 155 IPOs Nasdaq shows priced May–Oct 2026 (operating companies and SPACs
# agree to within a day). The windows are the p10–p90 band; the point is the
# median.
IMMINENT = {
    #          lo, point, hi
    "EFFECT": (0, 1, 2),
    "CERT":   (0, 1, 4),
    "8-A":    (0, 2, 9),
}

REG_FORMS = {"S-1", "S-1/A", "F-1", "F-1/A", "S-11", "S-11/A", "S-1MEF", "F-1MEF"}
PRICED_FORMS = {"424B4", "424B1"}
CERT_FORMS = {"CERT", "CERTNYS", "CERTNAS", "CERTARCA", "CERTPAC", "CERTAMEX", "CERTBATS"}
LISTING_FORMS = {"8-A12B", "8-A12G"}
LADDER_FORMS = REG_FORMS | PRICED_FORMS | CERT_FORMS | LISTING_FORMS | {"EFFECT", "RW", "DRS", "DRS/A"}

_SPAC_NAME = re.compile(r"\b(acquisition|spac|blank check)\b", re.I)


# ── small parsers ─────────────────────────────────────────────────────────────

def parse_mdy(s):
    if not s:
        return None
    for fmt in ("%m/%d/%Y", "%Y-%m-%d"):
        try:
            return datetime.strptime(str(s).strip()[:10], fmt).date()
        except ValueError:
            pass
    return None


def parse_money(s):
    """"$386,400,000" → 386400000.0; absent/"--" → None (never 0)."""
    if s is None:
        return None
    if isinstance(s, (int, float)):
        return float(s) if s > 0 else None
    t = re.sub(r"[^0-9.]", "", str(s))
    try:
        v = float(t)
    except ValueError:
        return None
    return v if v > 0 else None


def parse_range(s):
    """"$19.00-21.00" / "19.00-21.00" / "10.00" → (lo, hi) or None."""
    if not s:
        return None
    nums = re.findall(r"\d+(?:\.\d+)?", str(s).replace(",", ""))
    vals = [float(n) for n in nums[:2] if float(n) > 0]
    if not vals:
        return None
    return (min(vals), max(vals))


def is_spac(name, ticker=None, price=None, sic=None):
    """Blank-check companies price on a different clock and have no 180-day
    lock-up, so they are kept out of operating-company comparables. SIC 6770
    is authoritative when EDGAR is known; otherwise $10.00 units ("…U") or the
    name."""
    if sic:
        return str(sic) == "6770"
    t = (ticker or "").upper()
    if price is not None and abs(price - 10.0) < 1e-9 and len(t) >= 4 and t.endswith("U"):
        return True
    return bool(_SPAC_NAME.search(name or ""))


# ── cache ─────────────────────────────────────────────────────────────────────

class Cache:
    def __init__(self, path=None):
        self.con = sqlite3.connect(path or market_data_path("ipo_calendar.sqlite"), timeout=10)
        self.con.execute("CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT, at REAL)")

    def get(self, key, max_age_s):
        row = self.con.execute("SELECT v, at FROM kv WHERE k=?", (f"v{SCHEMA_V}:{key}",)).fetchone()
        if not row:
            return None
        if max_age_s is not None and time.time() - row[1] > max_age_s:
            return None
        return json.loads(row[0])

    def written_at(self, key):
        row = self.con.execute("SELECT at FROM kv WHERE k=?", (f"v{SCHEMA_V}:{key}",)).fetchone()
        return row[0] if row else None

    def stale(self, key):
        row = self.con.execute("SELECT v FROM kv WHERE k=?", (f"v{SCHEMA_V}:{key}",)).fetchone()
        return json.loads(row[0]) if row else None

    def put(self, key, value):
        self.con.execute("INSERT OR REPLACE INTO kv VALUES (?,?,?)",
                         (f"v{SCHEMA_V}:{key}", json.dumps(value), time.time()))
        self.con.commit()


_last = {"nasdaq": 0.0, "sec": 0.0}
# Request STARTS are spaced; the requests themselves run concurrently (Nasdaq
# answers in ~2.5 s, so one-at-a-time made a cold enrich take >10 minutes).
# SEC: ≤2.5 starts/s, the same budget as the other SEC scripts — up to three
# of them run at once (PreIpoService's S-1/Form D/N-PORT fetches start when
# IPO Watch opens) under SEC's shared 10/s cap.
_GAP = {"nasdaq": 0.12, "sec": 0.4}
_gate = threading.Lock()
WORKERS = 8


def _http_json(kind, url, headers):
    if requests is None:
        return None
    with _gate:
        wait = _GAP[kind] - (time.time() - _last[kind])
        if wait > 0:
            time.sleep(wait)
        _last[kind] = time.time()
    try:
        r = requests.get(url, headers=headers, timeout=20)
        r.raise_for_status()
        return r.json()
    except Exception as e:  # a failed request is not an answer — caller keeps stale
        print(f"ipo_calendar: {kind} fetch failed: {url.split('?')[0]} ({type(e).__name__})",
              file=sys.stderr)
        return None


# ── Nasdaq calendar ───────────────────────────────────────────────────────────

def _month_end_epoch(yyyymm):
    y, m = int(yyyymm[:4]), int(yyyymm[5:7])
    nxt = date(y + (m == 12), m % 12 + 1, 1)
    return time.mktime(nxt.timetuple())


def _month_ttl(yyyymm, today, written_at=None):
    """A closed month is re-read after 3 days (Nasdaq occasionally back-fills
    a row); the current and next month after 30 minutes. A copy taken while
    the month was still open (or within a day of its close — the last day's
    pricings land overnight) keeps the short TTL: the month's tail is missing."""
    if yyyymm >= today.strftime("%Y-%m"):
        return 30 * 60
    if written_at is not None and written_at < _month_end_epoch(yyyymm) + 86400:
        return 30 * 60
    return 3 * 86400


def _fetch_month_net(yyyymm):
    doc = _http_json("nasdaq", NASDAQ_CAL.format(yyyymm), NASDAQ_HEADERS)
    data = (doc or {}).get("data") if isinstance(doc, dict) else None
    if not isinstance(data, dict):
        return None
    return {
        "priced": ((data.get("priced") or {}).get("rows")) or [],
        "upcoming": (((data.get("upcoming") or {}).get("upcomingTable") or {}).get("rows")) or [],
        "filed": ((data.get("filed") or {}).get("rows")) or [],
        "withdrawn": ((data.get("withdrawn") or {}).get("rows")) or [],
    }


def fetch_months(cache, months, today):
    """→ (docs in month order, errors). Misses are fetched concurrently; a
    failed month falls back to its last cached copy and says so."""
    docs, errors, miss = {}, [], []
    for m in months:
        hit = cache.get(f"month:{m}", _month_ttl(m, today, cache.written_at(f"month:{m}")))
        if hit is not None:
            docs[m] = hit
        else:
            miss.append(m)
    with ThreadPoolExecutor(WORKERS) as ex:
        for m, got in zip(miss, ex.map(_fetch_month_net, miss)):
            if got is not None:
                cache.put(f"month:{m}", got)
                docs[m] = got
            else:
                old = cache.stale(f"month:{m}")
                docs[m] = old or {}
                errors.append(f"Nasdaq calendar {m} unavailable" + (" (showing cached)" if old else ""))
    return [docs[m] for m in months], errors


def months_to_read(today, back=MONTHS_BACK):
    # Current month and the next: an expected date early next month is filed
    # under next month. Further-forward months are always empty.
    out = []
    y, m = today.year, today.month
    for k in range(-back, 2):
        mm = m + k
        yy = y + (mm - 1) // 12
        mm = (mm - 1) % 12 + 1
        out.append(f"{yy:04d}-{mm:02d}")
    return out


def merge_months(month_docs):
    """Bucket rows → one dict per dealID. A deal with no dealID is keyed by
    name (rare; Nasdaq always sends one in practice)."""
    deals = {}

    def slot(r):
        did = (r.get("dealID") or "").strip() or ("name:" + (r.get("companyName") or "").strip().lower())
        d = deals.get(did)
        if d is None:
            d = deals[did] = {"deal_id": did, "company": "", "ticker": "", "exchange": "",
                              "filed_date": None, "expected_date": None, "priced_date": None,
                              "withdrawn_date": None, "price_range": "", "final_price": None,
                              "shares": None, "offer_amount": None}
        # latest non-empty identity wins (later buckets carry the listing name)
        for src, dst in (("companyName", "company"), ("proposedTickerSymbol", "ticker"),
                         ("proposedExchange", "exchange")):
            v = (r.get(src) or "").strip()
            if v:
                d[dst] = v
        return d

    for doc in month_docs:
        for r in doc.get("filed", []):
            d = slot(r)
            fd = parse_mdy(r.get("filedDate"))
            if fd and (d["filed_date"] is None or fd < d["filed_date"]):
                d["filed_date"] = fd
        for r in doc.get("upcoming", []):
            d = slot(r)
            d["expected_date"] = parse_mdy(r.get("expectedPriceDate")) or d["expected_date"]
            d["price_range"] = (r.get("proposedSharePrice") or "").strip() or d["price_range"]
            d["shares"] = parse_money(r.get("sharesOffered")) or d["shares"]
        for r in doc.get("priced", []):
            d = slot(r)
            d["priced_date"] = parse_mdy(r.get("pricedDate")) or d["priced_date"]
            rng = parse_range(r.get("proposedSharePrice"))
            # Priced rows carry the FINAL price in proposedSharePrice; a range
            # here would be malformed data, not a price.
            if rng and rng[0] == rng[1]:
                d["final_price"] = rng[0]
            d["shares"] = parse_money(r.get("sharesOffered")) or d["shares"]
            d["offer_amount"] = parse_money(r.get("dollarValueOfSharesOffered"))
        for r in doc.get("withdrawn", []):
            d = slot(r)
            d["withdrawn_date"] = parse_mdy(r.get("withdrawDate")) or d["withdrawn_date"]
            fd = parse_mdy(r.get("filedDate"))
            if fd and (d["filed_date"] is None or fd < d["filed_date"]):
                d["filed_date"] = fd

    for d in deals.values():
        d["spac"] = is_spac(d["company"], d["ticker"], d["final_price"])
        d["status"] = lifecycle_status(d)
    return deals


def lifecycle_status(d):
    if d.get("priced_date"):
        return "priced"
    if d.get("withdrawn_date"):
        return "withdrawn"
    if d.get("expected_date"):
        return "upcoming"
    return "filed"


# ── comparables: how long do filers like this take, if they price at all ──────

def comparables(deals, d, today):
    """Among deals of the same class first filed in this history, the ones
    still unresolved at this deal's age: how many went on to price, withdraw,
    or are still waiting — and the pricing lags of those that priced.
    The lag sample is censored (a deal filed 4 months ago can only show lags
    under 4 months), so the window leans early; the counts say how much."""
    fd = d.get("filed_date")
    if not fd:
        return None
    age = (today - fd).days
    at_risk = priced = withdrawn = pending = 0
    lags = []
    for o in deals.values():
        if o is d or o.get("spac") != d.get("spac") or not o.get("filed_date"):
            continue
        ofd = o["filed_date"]
        if (today - ofd).days < age:
            continue                       # not yet observed for this long
        p, w = o.get("priced_date"), o.get("withdrawn_date")
        if p and (p - ofd).days <= age:
            continue                       # had already priced by this age
        if w and (w - ofd).days <= age:
            continue                       # had already withdrawn
        at_risk += 1
        if p:
            priced += 1
            lags.append((p - ofd).days)
        elif w:
            withdrawn += 1
        else:
            pending += 1
    lags.sort()

    def q(f):
        return lags[min(len(lags) - 1, int(f * len(lags)))] if lags else None

    return {"age_days": age, "at_risk": at_risk, "priced": priced, "withdrawn": withdrawn,
            "pending": pending, "lag_q25": q(0.25), "lag_q50": q(0.5), "lag_q75": q(0.75)}


# ── EDGAR ladder ──────────────────────────────────────────────────────────────

def ladder_from_submissions(sub, filed_date):
    """Registration-ladder filings from the issuer's submissions JSON, on or
    after the first public filing (a company's earlier, abandoned attempt —
    or an unrelated shelf — must not count). DRS rows before it are kept: they
    are this deal's confidential drafts."""
    rec = ((sub or {}).get("filings") or {}).get("recent") or {}
    forms = rec.get("form") or []
    dates = rec.get("filingDate") or []
    accs = rec.get("accessionNumber") or []
    docs = rec.get("primaryDocument") or []
    fnos = rec.get("fileNumber") or []
    floor = (filed_date - timedelta(days=1)) if filed_date else None
    out = []
    for i, f in enumerate(forms):
        if f not in LADDER_FORMS:
            continue
        d = parse_mdy(dates[i] if i < len(dates) else "")
        if not d:
            continue
        if floor and d < floor and f not in ("DRS", "DRS/A"):
            continue
        if floor and f in ("DRS", "DRS/A") and d < floor - timedelta(days=730):
            continue
        out.append({"form": f, "date": d.isoformat(),
                    "accession": accs[i] if i < len(accs) else "",
                    "doc": docs[i] if i < len(docs) else "",
                    "file_no": fnos[i] if i < len(fnos) else ""})
    out.sort(key=lambda r: r["date"], reverse=True)
    return out


def stage_of(ladder):
    """(stage, date) of the furthest step reached. EFFECT / CERT / 8-A only
    count when filed after the latest registration amendment — an old EFFECT
    followed by a fresh S-1/A is a deal that slipped."""
    def latest(forms):
        ds = [parse_mdy(r["date"]) for r in ladder if r["form"] in forms]
        return max(ds) if ds else None

    # An RW withdraws ONE registration (its file number). A company that
    # re-files and then withdraws the abandoned earlier registration has not
    # withdrawn the live deal — only an RW against a registration on this
    # ladder counts (an RW with no file number on record is taken at its word).
    live_fnos = {r.get("file_no") for r in ladder if r["form"] in REG_FORMS and r.get("file_no")}
    rws = [parse_mdy(r["date"]) for r in ladder if r["form"] == "RW"
           and (not r.get("file_no") or not live_fnos or r["file_no"] in live_fnos)]
    rw, priced = (max(rws) if rws else None), latest(PRICED_FORMS)
    last_amend = latest(REG_FORMS - {"S-1MEF", "F-1MEF"})
    if rw and (not priced or rw >= priced) and (not last_amend or rw >= last_amend):
        return "withdrawn", rw
    if priced:
        return "priced", priced
    for stage, forms in (("effective", {"EFFECT"}), ("certified", CERT_FORMS),
                         ("listing", LISTING_FORMS)):
        d = latest(forms)
        if d and (not last_amend or d >= last_amend):
            return stage, d
    if last_amend:
        return "on_file", last_amend
    return "unknown", None


STALLED_AFTER = 21   # days past due before "overdue" becomes "stalled"


def _late(basis, due, today):
    """Past its window and not priced. Freshly late is news (postponed?);
    weeks late is a deal that is not happening on this schedule."""
    kind = "overdue" if (today - due).days <= STALLED_AFTER else "stalled"
    return {"kind": kind, "basis": basis}


def project(d, comps, today):
    """The projected pricing date and what it rests on."""
    st, st_date = d.get("stage"), parse_mdy(d.get("stage_date"))
    if d.get("priced_from") == "edgar":
        return {"kind": "priced", "date": st_date.isoformat(),
                "basis": f"Final prospectus (424B) filed {st_date:%b %-d}; Nasdaq not updated yet"}
    if d.get("withdrawn_from") == "edgar":
        return {"kind": "withdrawn", "basis": f"RW (withdrawal) filed {st_date:%b %-d}; Nasdaq not updated yet"}
    if d["status"] == "priced":
        return {"kind": "priced", "date": d["priced_date"].isoformat(), "basis": "Nasdaq priced row"}
    if d["status"] == "withdrawn":
        return {"kind": "withdrawn", "basis": "Nasdaq withdrawn row"}
    if st == "withdrawn":
        return {"kind": "withdrawn", "basis": f"RW (withdrawal) filed {st_date:%b %-d}"}
    if st == "priced":
        return {"kind": "priced", "date": st_date.isoformat(),
                "basis": f"Final prospectus (424B) filed {st_date:%b %-d}; Nasdaq not updated yet"}

    # The underwriters' date, while still ahead, outranks an EDGAR window that
    # has lapsed: a deal filed its 8-A early and is pricing on schedule.
    exp = d.get("expected_date")
    if exp and exp >= today:
        return {"kind": "scheduled", "date": exp.isoformat(),
                "basis": "Nasdaq expected price date (set by the underwriters; can slip)"}

    imminent = {"effective": ("EFFECT", "SEC declared the registration effective"),
                "certified": ("CERT", "exchange certified the listing"),
                "listing": ("8-A", "exchange registration (8-A) filed")}
    if st in imminent:
        key, what = imminent[st]
        lo, pt, hi = IMMINENT[key]
        due_hi = st_date + timedelta(days=hi)
        if today <= due_hi:
            return {"kind": "imminent", "date": max(st_date + timedelta(days=pt), today).isoformat(),
                    "lo": (st_date + timedelta(days=lo)).isoformat(), "hi": due_hi.isoformat(),
                    "basis": f"{what} {st_date:%b %-d}; IPOs at this step priced within "
                             f"{lo}–{hi} days (p10–p90)"}
        return _late(f"{what} {st_date:%b %-d}, but no pricing reported since — postponed, or a "
                     "best-efforts offering selling over time", due_hi, today)

    if exp:
        return _late(f"was expected {exp:%b %-d}; no pricing reported since", exp, today)

    last = parse_mdy(d.get("last_filing_date"))
    if last and (today - last).days > DORMANT_DAYS:
        return {"kind": "dormant", "basis": f"no registration filing since {last:%b %-d, %Y}"}

    if not comps or comps["priced"] < MIN_COMPARABLES:
        n = comps["priced"] if comps else 0
        return {"kind": "none", "comps": comps,
                "basis": f"too few comparable filers priced ({n}) to estimate"}
    share = comps["priced"] / comps["at_risk"]
    if share < MIN_SHARE:
        return {"kind": "long_shot", "comps": comps, "share": share,
                "basis": (f"only {comps['priced']} of {comps['at_risk']} comparable filers still "
                          f"unpriced at {comps['age_days']}d went on to price ({share:.0%}) — "
                          "no date is projected")}
    fd = d["filed_date"]
    point = fd + timedelta(days=comps["lag_q50"])
    lo = max(fd + timedelta(days=comps["lag_q25"]), today)
    hi = max(fd + timedelta(days=comps["lag_q75"]), lo)
    cls = "SPACs" if d.get("spac") else "operating companies"
    return {"kind": "estimate", "date": max(point, lo).isoformat(), "lo": lo.isoformat(),
            "hi": hi.isoformat(), "comps": comps, "share": share,
            "basis": (f"{comps['priced']} of {comps['at_risk']} {cls} still unpriced at this age "
                      f"({comps['age_days']}d after first filing) went on to price — "
                      f"median {comps['lag_q50']}d after filing; {comps['withdrawn']} withdrew, "
                      f"{comps['pending']} still waiting")}


# ── enrichment ────────────────────────────────────────────────────────────────

def _ov_ttl(final):
    # A priced deal's overview is settled. A pending one changes when the
    # range is set — re-read every 2 days (the stage comes from EDGAR, which
    # is read far more often).
    return 365 * 86400 if final else 2 * 86400


def _fetch_overview_net(deal_id):
    doc = _http_json("nasdaq", NASDAQ_DEAL.format(deal_id), NASDAQ_HEADERS)
    po = (((doc or {}).get("data") or {}).get("poOverview")) if isinstance(doc, dict) else None
    return parse_overview(po) if isinstance(po, dict) else None


def overviews(cache, wanted):
    """{deal_id: overview|None} for [(deal_id, final)], misses fetched
    concurrently; a failed fetch falls back to the stale copy."""
    out, miss = {}, []
    for did, final in wanted:
        if did.startswith("name:"):
            out[did] = None
            continue
        hit = cache.get(f"ov:{did}", _ov_ttl(final))
        # A copy taken while the deal was pending is not settled: the lock-up
        # term, final shares and over-allotment arrive with the prospectus.
        if hit is not None and final and not hit.get("_final"):
            hit = None
        if hit is not None:
            out[did] = hit
        else:
            miss.append(did)
    final_of = dict(wanted)
    with ThreadPoolExecutor(WORKERS) as ex:
        for did, ov in zip(miss, ex.map(_fetch_overview_net, miss)):
            if ov is not None:
                ov["_final"] = bool(final_of.get(did))
                cache.put(f"ov:{did}", ov)
            out[did] = ov if ov is not None else cache.stale(f"ov:{did}")
    return out


def parse_overview(po):
    """Nasdaq's poOverview → the fields we use. "--" / "" are absent (None),
    never zero."""
    def val(k):
        v = (po.get(k) or {}).get("value")
        if v is None:
            return None
        t = str(v).strip()
        return None if t in ("", "--", "N/A") else v

    lock = val("LockupPeriodNumberofDays")
    try:
        lock = int(float(str(lock).replace(",", ""))) if lock is not None else None
    except ValueError:
        lock = None
    cik = val("SECCIK")
    cik = re.sub(r"\D", "", str(cik)).lstrip("0") if cik else None
    return {"cik": cik or None, "price_range": val("ProposedSharePrice"),
            "shares": parse_money(val("SharesOffered")),
            "overallot": parse_money(val("SharesOverAllotment")),
            "lockup_days": lock if lock and lock > 0 else None,
            "exchange": val("Exchange")}


def _sub_ttl(hot):
    return 3600 if hot else 6 * 3600


def _fetch_submissions_net(cik):
    doc = _http_json("sec", SEC_SUBMISSIONS.format(str(cik).zfill(10)),
                     {"User-Agent": user_agent(), "Accept-Encoding": "gzip, deflate"})
    if not isinstance(doc, dict):
        return None
    # keep only what the ladder needs — submissions JSON can be megabytes
    rec = (doc.get("filings") or {}).get("recent") or {}
    keep = [i for i, f in enumerate(rec.get("form") or []) if f in LADDER_FORMS]
    cols = ("form", "filingDate", "accessionNumber", "primaryDocument", "fileNumber")
    return {"sic": doc.get("sic"), "name": doc.get("name"),
            "filings": {"recent": {k: [(rec.get(k) or [])[i] for i in keep
                                       if i < len(rec.get(k) or [])] for k in cols}}}


def submissions_many(cache, wanted):
    """{cik: slim submissions|None} for [(cik, hot)]."""
    out, miss = {}, []
    for cik, hot in wanted:
        if cik in out or cik in miss:
            continue
        hit = cache.get(f"sub2:{cik}", _sub_ttl(hot))
        if hit is not None:
            out[cik] = hit
        else:
            miss.append(cik)
    with ThreadPoolExecutor(4) as ex:
        for cik, sub in zip(miss, ex.map(_fetch_submissions_net, miss)):
            if sub is not None:
                cache.put(f"sub2:{cik}", sub)
                out[cik] = sub
            else:
                out[cik] = cache.stale(f"sub2:{cik}")
                out.setdefault("__failed__", []).append(cik)
    return out


def apply_overview(d, ov):
    if not ov:
        return
    d["cik"] = (str(ov["cik"]).lstrip("0") or None) if ov.get("cik") else None
    d["lockup_days"] = ov.get("lockup_days")
    d["overallot"] = ov.get("overallot")
    if not d.get("price_range") and ov.get("price_range") and d["status"] != "priced":
        d["price_range"] = str(ov["price_range"]).lstrip("$")
        d["range_from"] = "overview"
    if not d.get("shares") and ov.get("shares"):
        d["shares"] = ov["shares"]
    if not d.get("exchange") and ov.get("exchange"):
        d["exchange"] = ov["exchange"]


def apply_submissions(d, sub, today):
    if not sub:
        return
    if sub.get("sic"):
        d["sic"] = sub["sic"]
        d["spac"] = is_spac(d["company"], sic=sub["sic"])
    # No filing date in the window (first filed >13 months ago; only the
    # upcoming row is here): bound the ladder anyway, or an old 424B4 from an
    # earlier offering reads as this deal having priced.
    floor = d.get("filed_date") or ((d.get("expected_date") or today) - timedelta(days=365))
    ladder = ladder_from_submissions(sub, floor)
    d["ladder"] = ladder[:24]
    d["stage"], sd = stage_of(ladder)
    d["stage_date"] = sd.isoformat() if sd else None
    reg = [parse_mdy(r["date"]) for r in ladder if r["form"] not in ("DRS", "DRS/A")]
    d["last_filing_date"] = max(reg).isoformat() if reg else None
    # EDGAR runs ahead of Nasdaq: a 424B4 or an RW is the answer even when
    # the calendar has not caught up.
    if d["stage"] == "priced" and d["status"] != "priced":
        d["status"], d["priced_date"], d["priced_from"] = "priced", sd, "edgar"
    elif d["stage"] == "withdrawn" and d["status"] != "withdrawn":
        d["status"], d["withdrawn_date"], d["withdrawn_from"] = "withdrawn", sd, "edgar"


def _ser(d):
    return {k: (v.isoformat() if isinstance(v, date) else v) for k, v in d.items()}


def build(today=None, enrich=False, cache=None):
    today = today or date.today()
    cache = cache or Cache()
    months = months_to_read(today)
    docs, errors = fetch_months(cache, months, today)
    deals = merge_months(docs)

    lockups = []
    if enrich:
        horizon = today - timedelta(days=DORMANT_DAYS * 3)
        pending = [d for d in deals.values() if d["status"] in ("upcoming", "filed")
                   and (d.get("filed_date") or today) >= horizon]
        for d in deals.values():
            if d["status"] in ("upcoming", "filed") and (d.get("filed_date") or today) < horizon:
                d["edgar_skipped"] = True
        recent_priced = [d for d in deals.values() if d.get("priced_date") and not d.get("spac")
                         and (today - d["priced_date"]).days <= LOCKUP_LOOKBACK]
        ovs = overviews(cache, [(d["deal_id"], False) for d in pending] +
                               [(d["deal_id"], True) for d in recent_priced])
        for d in pending + recent_priced:
            apply_overview(d, ovs.get(d["deal_id"]))
        subs = submissions_many(cache, [(d["cik"], d["status"] == "upcoming")
                                        for d in pending if d.get("cik")])
        failed = subs.pop("__failed__", [])
        for d in pending:
            if d.get("cik"):
                sub = subs.get(d["cik"])
                apply_submissions(d, sub, today)
                d["edgar_failed"] = sub is None
        if failed:
            errors.append(f"SEC EDGAR unavailable for {len(failed)} issuer(s) — their stage is "
                          "unknown (SEC refuses clients without Settings › Credentials › Contact email)")
        lockups = lockup_rows(deals, today)

    # Project only after every stage is known: comparables read the others.
    # Terminal deals return before reading comparables — don't build them.
    for d in deals.values():
        live = d["status"] in ("upcoming", "filed") and d.get("stage") not in ("priced", "withdrawn")
        d["projection"] = project(d, comparables(deals, d, today) if live else None, today)

    return {"as_of": today.isoformat(), "enriched": enrich,
            "deals": sorted((_ser(d) for d in deals.values()), key=lambda r: r["deal_id"]),
            "lockups": lockups, "errors": errors, "months": months}


LOCKUP_LOOKBACK = 400   # longest term we expect (365) plus slack


def lockup_rows(deals, today, ahead=180, behind=7):
    """Operating-company IPOs whose lock-up ends in [today-behind, today+ahead],
    using each deal's own term. Early-release clauses (price- or
    earnings-triggered, staggered tranches) and waivers are NOT modelled — the
    date is when the prospectus term lapses."""
    out = []
    for d in deals.values():
        p = d.get("priced_date")
        if not p or d.get("spac") or (today - p).days > LOCKUP_LOOKBACK:
            continue
        days = d.get("lockup_days")
        basis = "term" if days else "assumed"
        days = days or LOCKUP_DEFAULT
        exp = p + timedelta(days=days)
        if not (today - timedelta(days=behind) <= exp <= today + timedelta(days=ahead)):
            continue
        out.append({"deal_id": d["deal_id"], "ticker": d["ticker"], "company": d["company"],
                    "priced_date": p.isoformat(), "lockup_days": days, "expiry": exp.isoformat(),
                    "basis": basis, "shares_offered": d.get("shares"),
                    "final_price": d.get("final_price")})
    out.sort(key=lambda r: r["expiry"])
    return out


def main():
    action = sys.argv[1] if len(sys.argv) > 1 else "calendar"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    today = parse_mdy(payload.get("today")) if payload.get("today") else None
    if action not in ("calendar", "enrich"):
        print(json.dumps({"error": f"Unknown action: {action}"}))
        return
    print(json.dumps(build(today=today, enrich=action == "enrich"), default=str))


if __name__ == "__main__":
    main()
