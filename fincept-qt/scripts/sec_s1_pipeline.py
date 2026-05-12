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
FACTS_URL = "https://data.sec.gov/api/xbrl/companyfacts/CIK{cik}.json"

UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
}

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

    raw.sort(key=lambda h: (h.get("_source", {}).get("file_date") or
                            h.get("_source", {}).get("filed_date") or ""))

    out_by_cik = {}
    for h in raw:
        if len(out_by_cik) >= max_hits and h.get("_source", {}).get("ciks", [""])[0] not in (
                cikv for cikv in out_by_cik):
            # Cap distinct filers; still allow updates to existing ones.
            pass
        src = h.get("_source", {})
        ciks = src.get("ciks") or []
        names = src.get("display_names") or []
        if not ciks or not names:
            continue
        cik = _cik_padded(ciks[0])
        if cik not in out_by_cik and len(out_by_cik) >= max_hits:
            continue
        name = _clean_name(names[0])
        form = src.get("form") or (src.get("forms", [""]) or [""])[0]
        filed = src.get("file_date") or ""
        is_amend = "/A" in (form or "")
        entry = out_by_cik.setdefault(cik, {
            "cik": cik,
            "company_name": name,
            "first_filed": filed,
            "latest_amended": "",
            "amendment_count": 0,
            "form_types": [],
            "days_since_first": 0,
            "edgar_url": f"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK={cik}&type=S-1",
            "filings": [],
        })
        entry["filings"].append({"form": form, "filed_date": filed, "adsh": h.get("_id", "")})
        if filed and (not entry["first_filed"] or filed < entry["first_filed"]):
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


def handle_action(action, payload):
    if action == "pipeline_all":
        return fetch_pipeline(
            days_back=payload.get("days_back", 180),
            max_hits=payload.get("max_hits", 200),
        )
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
