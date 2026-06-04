"""
SEC EDGAR S-1 / F-1 IPO pipeline + XBRL financials.

Extracts:
  - S-1 / S-1/A / F-1 / F-1/A / S-11 filings, grouped by issuer (CIK).
  - Amendment cadence per issuer (latest 12 months).
  - For each issuer: financial facts from data.sec.gov/api/xbrl/companyfacts/
    (revenue, net income, gross margin, cash). XBRL filings start appearing
    on the S-1 itself for emerging-growth-company filers.

Actions:
  pipeline_all   — full S-1 universe (last N days) with amendment counts.
  facts_for      — payload: {"cik": "..."} → financial facts dict.
"""

import json
import re
import sys
import time
from datetime import date, timedelta

try:
    import requests
except ImportError:
    requests = None

EDGAR_SEARCH = "https://efts.sec.gov/LATEST/search-index"
EDGAR_SUBMISSIONS = "https://data.sec.gov/submissions"
FACTS_URL = "https://data.sec.gov/api/xbrl/companyfacts/CIK{cik}.json"

UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
}

# Registration-statement form family. /A entries are amendments; DRS / DRS/A are
# confidential draft submissions filed *before* the public S-1 (the EGC path).
S1_FORMS = ("S-1", "S-1/A", "F-1", "F-1/A", "S-11", "S-11/A", "DRS", "DRS/A")
# Draft-registration forms: listed in the timeline but excluded from the
# first_filed "clock" so the readiness estimate counts from the public S-1, not
# the months-earlier confidential draft.
_DRAFT_FORMS = ("DRS", "DRS/A")

# Curated CIKs (shared with sec_form_d_data.py). The generic efts sweep is hit-
# capped and ordered by date, so a marquee late filer like SpaceX can fall off
# the list entirely; force-fetching these guarantees they always appear.
try:
    from sec_known_ciks import KNOWN_PRIVATE_CIKS
except ImportError:
    KNOWN_PRIVATE_CIKS = ["0001181412", "0001691342"]  # SpaceX, Stripe

_LAST_REQ = 0.0
# 0.4s gap = ≤2.5 req/s per process; with up to 3 SEC scripts concurrent the
# combined load stays well under the 10 req/s shared cap.
_MIN_REQ_GAP = 0.4


def _get(url, params=None, headers=None, timeout=15):
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < _MIN_REQ_GAP:
        time.sleep(_MIN_REQ_GAP - elapsed)
    try:
        r = requests.get(url, params=params, timeout=timeout, headers=headers or UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        return None


def _cik_padded(cik):
    return str(cik).zfill(10)


def _clean_name(s):
    return re.sub(r"\s*\(CIK.*$", "", (s or "")).strip()


# ── Pipeline ──────────────────────────────────────────────────────────────────

def _filings_for_cik(cik, limit=12):
    """Force-fetch a curated CIK's recent registration-statement filings from
    the submissions API, shaped like efts hits so they merge into `raw` with no
    special-casing downstream. Independent of the date window and hit cap, so a
    name like SpaceX always appears even when the generic sweep misses it."""
    cik_pad = _cik_padded(cik)
    r = _get(f"{EDGAR_SUBMISSIONS}/CIK{cik_pad}.json",
             headers={**UA, "Host": "data.sec.gov"})
    if r is None:
        print(f"sec_s1_pipeline: submissions lookup failed for CIK {cik_pad}",
              file=sys.stderr)
        return []
    try:
        data = r.json()
    except Exception:
        return []
    recent = data.get("filings", {}).get("recent", {})
    forms = recent.get("form", []) or []
    dates = recent.get("filingDate", []) or []
    adshes = recent.get("accessionNumber", []) or []
    name = data.get("name") or ""
    forms_set = set(S1_FORMS)
    out = []
    for i, f in enumerate(forms):
        if f not in forms_set:
            continue
        out.append({
            "_id": adshes[i] if i < len(adshes) else "",
            "_source": {
                "ciks": [cik_pad],
                "display_names": [name],
                "form": f,
                "file_date": dates[i] if i < len(dates) else "",
            },
            "_curated": True,
        })
        if len(out) >= limit:
            break
    return out


def fetch_pipeline(days_back=180, max_hits=200):
    start = (date.today() - timedelta(days=days_back)).isoformat()
    end = date.today().isoformat()

    # Collect all hits first, then process oldest-first so amendment counts
    # are stable even when EDGAR pagination returns newest-first.
    raw = []
    from_ = 0
    while True:
        params = {
            "forms": "S-1,S-1/A,F-1,F-1/A,S-11,S-11/A",
            "dateRange": "custom",
            "startdt": start,
            "enddt": end,
            "from": from_,
        }
        r = _get(EDGAR_SEARCH, params=params, headers={**UA, "Host": "efts.sec.gov"})
        if r is None:
            break
        try:
            data = r.json()
        except Exception:
            break
        hits = data.get("hits", {}).get("hits", [])
        if not hits:
            break
        raw.extend(hits)
        if len(raw) >= max_hits * 4 or len(hits) < 10:
            break
        from_ += len(hits)

    # Force-fetch curated CIKs so marquee filers (SpaceX, …) always appear,
    # independent of the date window and the distinct-filer cap below.
    curated = {_cik_padded(c) for c in KNOWN_PRIVATE_CIKS}
    for cik in KNOWN_PRIVATE_CIKS:
        raw.extend(_filings_for_cik(cik))

    raw.sort(key=lambda h: (h.get("_source", {}).get("file_date") or
                            h.get("_source", {}).get("filed_date") or ""))

    out_by_cik = {}
    for h in raw:
        src = h.get("_source", {})
        ciks = src.get("ciks") or []
        names = src.get("display_names") or []
        if not ciks or not names:
            continue
        cik = _cik_padded(ciks[0])
        # Cap distinct filers, but never evict a curated CIK.
        if cik not in out_by_cik and cik not in curated and len(out_by_cik) >= max_hits:
            continue
        name = _clean_name(names[0])
        form = src.get("form") or (src.get("forms", [""]) or [""])[0]
        filed = src.get("file_date") or ""
        is_draft = form in _DRAFT_FORMS
        # Only public amendments count toward the readiness/burst signal — a
        # DRS/A is a confidential-draft revision, not an S-1 amendment.
        is_amend = ("/A" in (form or "")) and not is_draft
        entry = out_by_cik.setdefault(cik, {
            "cik": cik,
            "company_name": name,
            "first_filed": "",   # earliest PUBLIC registration (see below)
            "_first_any": "",    # earliest of any form (draft fallback)
            "_seen": set(),      # accession dedup (efts ∩ curated overlap)
            "latest_amended": "",
            "amendment_count": 0,
            "form_types": [],
            "days_since_first": 0,
            "edgar_url": f"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK={cik}&type=S-1",
            "filings": [],
        })
        # A filing can arrive from both the generic sweep and the curated
        # submissions fetch (and efts emits one hit per document in a filing).
        # Dedup on accession so amendment counts aren't inflated. efts _id is
        # "<accession>:<doc>"; the curated path supplies the bare accession.
        adsh = (h.get("_id") or "").split(":", 1)[0]
        dkey = adsh or f"{form}|{filed}"
        if dkey in entry["_seen"]:
            continue
        entry["_seen"].add(dkey)
        # raw is sorted ascending by date, so the last write wins → the most
        # recent display name (handles renames and stale early-filing names).
        if name: entry["company_name"] = name
        entry["filings"].append({"form": form, "filed_date": filed, "adsh": adsh})
        if filed and (not entry["_first_any"] or filed < entry["_first_any"]):
            entry["_first_any"] = filed
        # The readiness clock starts at the first PUBLIC S-1/F-1, not the
        # months-earlier confidential DRS draft.
        if filed and not is_draft and (not entry["first_filed"] or filed < entry["first_filed"]):
            entry["first_filed"] = filed
        if is_amend:
            entry["amendment_count"] += 1
            if filed > (entry["latest_amended"] or ""):
                entry["latest_amended"] = filed
        if form and form not in entry["form_types"]:
            entry["form_types"].append(form)

    today = date.today()
    out = []
    for entry in out_by_cik.values():
        # Confidential-only filers (DRS but no public S-1 yet) fall back to the
        # draft date so the row still carries a timeline.
        if not entry["first_filed"]:
            entry["first_filed"] = entry["_first_any"]
        entry.pop("_first_any", None)
        entry.pop("_seen", None)
        try:
            d = date.fromisoformat(entry["first_filed"])
            entry["days_since_first"] = (today - d).days
        except Exception:
            entry["days_since_first"] = 0
        entry["filings"].sort(key=lambda f: f.get("filed_date", ""), reverse=True)
        out.append(entry)
    out.sort(key=lambda c: c.get("first_filed", ""), reverse=True)
    return out


# ── XBRL company facts ────────────────────────────────────────────────────────

_CONCEPT_PRIORITY = {
    "revenue": [
        "Revenues",
        "RevenueFromContractWithCustomerExcludingAssessedTax",
        "RevenueFromContractWithCustomerIncludingAssessedTax",
        "SalesRevenueNet",
    ],
    "net_income": ["NetIncomeLoss"],
    "gross_profit": ["GrossProfit"],
    "cash": ["CashAndCashEquivalentsAtCarryingValue", "Cash"],
}


def _annual_value(concept, prefer="USD"):
    """Pick the latest annual value from a companyconcept node.

    XBRL companies report annual figures as `fp=="FY"` (fiscal year) or
    `fp=="CY"` (calendar year). Some filers also leave `fp` empty for
    annual frames identified only by `frame="CY{YYYY}"`. We accept any
    of those — picking just FY caused 0-revenue for calendar-year filers
    and silently flunked them on the readiness gate.
    """
    units = concept.get("units", {})
    bucket = units.get(prefer) or next(iter(units.values()), [])
    best = None
    for row in bucket:
        fp = (row.get("fp") or "").upper()
        frame = (row.get("frame") or "")
        # Accept either fp=FY|CY or a frame string that names a calendar year
        # (e.g. "CY2025" with no quarter suffix).
        is_annual = (fp in ("FY", "CY")) or (
            frame.startswith("CY") and "Q" not in frame and len(frame) >= 6
        )
        if not is_annual:
            continue
        if best is None or (row.get("end", "") > best.get("end", "")):
            best = row
    return best


def fetch_company_facts(cik):
    cik_pad = _cik_padded(cik)
    r = _get(FACTS_URL.format(cik=cik_pad), headers={**UA, "Host": "data.sec.gov"})
    if r is None:
        return None
    try:
        data = r.json()
    except Exception:
        return None
    facts = data.get("facts", {}).get("us-gaap", {})

    def latest(concept_names):
        for n in concept_names:
            node = facts.get(n)
            if not node:
                continue
            val = _annual_value(node)
            if val:
                return val
        return None

    rev = latest(_CONCEPT_PRIORITY["revenue"])
    ni = latest(_CONCEPT_PRIORITY["net_income"])
    gp = latest(_CONCEPT_PRIORITY["gross_profit"])
    cash = latest(_CONCEPT_PRIORITY["cash"])

    out = {
        "revenue_m": (rev["val"] / 1_000_000.0) if rev else 0.0,
        "revenue_end": rev["end"] if rev else "",
        "revenue_fy": rev.get("fy") if rev else "",
        "net_income_m": (ni["val"] / 1_000_000.0) if ni else 0.0,
        "gross_margin_pct": (
            (gp["val"] / rev["val"] * 100.0) if (rev and gp and rev["val"]) else 0.0
        ),
        "cash_m": (cash["val"] / 1_000_000.0) if cash else 0.0,
    }

    # Year-over-year growth from previous FY revenue
    prev = None
    if rev:
        for c in _CONCEPT_PRIORITY["revenue"]:
            node = facts.get(c)
            if not node:
                continue
            units = node.get("units", {})
            usd = units.get("USD") or []
            fy_rows = [r for r in usd if r.get("fp") == "FY" and r.get("end", "") < rev.get("end", "")]
            if fy_rows:
                fy_rows.sort(key=lambda x: x.get("end", ""), reverse=True)
                prev = fy_rows[0]
                break
    if prev and rev and prev["val"] > 0:
        out["revenue_growth_yoy_pct"] = (rev["val"] - prev["val"]) / prev["val"] * 100.0
    else:
        out["revenue_growth_yoy_pct"] = 0.0
    return out


def pipeline_newest(days_back=180):
    """Data-based change signal for the IPO pipeline: the newest S-1/F-1 filing
    date. One efts page-1 request (EDGAR returns newest-first). We use the max
    date rather than a total count because a broad all-time count saturates
    efts's 10k cap (and a sliding-window count churns daily as filings age off);
    the newest date only advances when something genuinely new is filed."""
    start = (date.today() - timedelta(days=days_back)).isoformat()
    end = date.today().isoformat()
    r = _get(EDGAR_SEARCH,
             params={"forms": "S-1,S-1/A,F-1,F-1/A,S-11,S-11/A",
                     "dateRange": "custom", "startdt": start, "enddt": end, "from": 0},
             headers={**UA, "Host": "efts.sec.gov"})
    newest = ""
    if r is not None:
        try:
            for h in r.json().get("hits", {}).get("hits", []):
                d = h.get("_source", {}).get("file_date") or ""
                if d > newest:
                    newest = d
        except Exception:
            pass
    return newest


def handle_action(action, payload):
    if action == "pipeline_all":
        # Cheap newest-date probe first: if unchanged since the prior cursor,
        # skip the full paged fetch. Returns an object (not a bare array) so the
        # cursor + unchanged flag have somewhere to live.
        #
        # The cursor carries a version: bump CURSOR_VERSION whenever the fetch
        # logic changes what `pipeline` contains (e.g. the curated-CIK injection
        # below), so a stale pre-change cursor forces one full re-fetch instead
        # of replaying an outdated cached result. `pipeline_newest` only probes
        # efts, which doesn't see force-fetched curated CIKs.
        CURSOR_VERSION = 2  # bumped: curated KNOWN_PRIVATE_CIKS injection added
        days = payload.get("days_back", 180)
        prev = payload.get("prev_cursor")
        newest = pipeline_newest(days)
        if (newest and isinstance(prev, dict)
                and prev.get("newest") == newest
                and prev.get("v") == CURSOR_VERSION):
            return {"unchanged": True, "cursor": {"newest": newest, "v": CURSOR_VERSION}}
        pipeline = fetch_pipeline(days_back=days, max_hits=payload.get("max_hits", 200))
        return {"pipeline": pipeline, "cursor": {"newest": newest, "v": CURSOR_VERSION}}
    if action == "facts_for":
        cik = payload.get("cik")
        if not cik:
            return {"error": "cik required"}
        f = fetch_company_facts(cik)
        return f or {}
    if action == "pipeline_with_facts":
        # Pipeline + financials in one shot, capped to top N filers.
        cap = payload.get("max_facts", 30)
        pipeline = fetch_pipeline(
            days_back=payload.get("days_back", 180),
            max_hits=payload.get("max_hits", 100),
        )
        for entry in pipeline[:cap]:
            facts = fetch_company_facts(entry["cik"])
            entry["financials"] = facts or {}
        return pipeline
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "pipeline_all"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(action, payload), indent=2, default=str))
