"""
SEC EDGAR Form D & S-1/F-1 Pipeline Monitor
Fetches private placement filings (Form D) and IPO filings (S-1, F-1) from EDGAR.

All data is public — no API key required.
EDGAR full-text search: https://efts.sec.gov/LATEST/search-index

Actions (PythonRunner):
  all_data           — combined {form_d_companies, s1_filings, recent_form_d}
                       for PreIpoService.parse_sec_summary()
  form_d_filings     — recent Reg D private placements (flat list)
  ipo_pipeline       — recent S-1 / F-1 / S-11 filings (flat list)
"""

import json
import re
from datetime import date, timedelta, datetime

try:
    import requests
except ImportError:
    requests = None

EDGAR_SEARCH = "https://efts.sec.gov/LATEST/search-index"
EDGAR_SUBMISSIONS = "https://data.sec.gov/submissions"


def _get(url: str, params: dict = None, timeout: int = 20) -> dict:
    if requests is None:
        return {"error": "requests not available"}
    try:
        headers = {
            "User-Agent": "FinceptTerminal research@fincept.com",
            "Accept": "application/json",
        }
        r = requests.get(url, params=params, timeout=timeout, headers=headers)
        r.raise_for_status()
        return r.json()
    except Exception as e:
        return {"error": str(e)}


# ── Form D ────────────────────────────────────────────────────────────────────

def fetch_form_d(days_back: int = 7) -> list:
    """
    Fetch recent Form D filings from EDGAR full-text search.
    Form D = Regulation D private placement notice.
    """
    start = (date.today() - timedelta(days=days_back)).isoformat()
    params = {
        "forms": "D",
        "dateRange": "custom",
        "startdt": start,
        "enddt": date.today().isoformat(),
        "_source": "file_date,entity_name,period_of_report,form_type",
        "hits.hits._source": "true",
        "hits.hits.total": "true",
    }
    data = _get(EDGAR_SEARCH, params)
    if "error" in data:
        return _fallback_form_d()

    hits = data.get("hits", {}).get("hits", [])
    result = []
    for hit in hits[:50]:  # cap at 50
        src = hit.get("_source", {})
        filing = {
            "company_name": src.get("entity_name", "Unknown"),
            "filed_date": src.get("file_date", ""),
            "form_type": src.get("form_type", "D"),
            "amount_raised": 0,  # requires parsing the full filing XML
            "exemption": "",
            "offering_type": "Equity",
            "state": "",
            "edgar_url": f"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK={src.get('entity_id','')}&type=D",
        }
        result.append(filing)

    return result  # empty list when EDGAR is unreachable — caller shows error state


# ── IPO Pipeline (S-1 / F-1 monitor) ─────────────────────────────────────────

def fetch_ipo_pipeline(days_back: int = 90) -> list:
    """
    Fetch recent S-1, F-1, and S-11 filings — the IPO pipeline.
    """
    start = (date.today() - timedelta(days=days_back)).isoformat()
    params = {
        "forms": "S-1,F-1,S-11",
        "dateRange": "custom",
        "startdt": start,
        "enddt": date.today().isoformat(),
    }
    data = _get(EDGAR_SEARCH, params)
    if "error" in data:
        return _fallback_ipo_pipeline()

    hits = data.get("hits", {}).get("hits", [])
    result = []
    for hit in hits[:30]:
        src = hit.get("_source", {})
        is_amendment = src.get("form_type", "").endswith("/A")
        result.append({
            "company_name": src.get("entity_name", "Unknown"),
            "filed_date": src.get("file_date", ""),
            "form_type": src.get("form_type", "S-1"),
            "offering_size_usd": 0,
            "underwriters": [],
            "edgar_url": f"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK={src.get('entity_id','')}&type=S-1",
            "is_amendment": is_amendment,
        })

    return result  # empty list when EDGAR is unreachable — caller shows error state


# ── Daemon action handler ─────────────────────────────────────────────────────

def build_all_data(days_back_fd: int = 30, days_back_ipo: int = 90) -> dict:
    """
    Build combined summary for PreIpoService.parse_sec_summary().
    Returns:
      form_d_companies — Form D filers grouped by company, with rounds array
      s1_filings       — Recent S-1/F-1 IPO pipeline
      recent_form_d    — Flat list of all recent Form D filings
    """
    form_d = fetch_form_d(days_back=days_back_fd)
    ipo    = fetch_ipo_pipeline(days_back=days_back_ipo)

    # Group Form D filings by company to create a company + rounds structure.
    # Multiple Form D filings from the same issuer become separate rounds.
    companies: dict = {}
    for f in form_d:
        name = f.get("company_name", "").strip()
        if not name:
            continue
        cid = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
        if cid not in companies:
            companies[cid] = {
                "id":     cid,
                "name":   name,
                "sector": "",   # Form D does not disclose sector
                "state":  f.get("state", ""),
                "rounds": [],
            }
        companies[cid]["rounds"].append({
            "date":      f.get("filed_date", ""),
            "amount_m":  f.get("amount_raised", 0),
            "exemption": f.get("exemption", ""),
        })

    # Map S-1 filers to the keys expected by parse_sec_summary
    s1_filings = [
        {
            "company_name": f.get("company_name", ""),
            "filed_date":   f.get("filed_date", ""),
            "edgar_url":    f.get("edgar_url", ""),
        }
        for f in ipo
        if f.get("company_name")
    ]

    return {
        "form_d_companies": list(companies.values()),
        "s1_filings":       s1_filings,
        "recent_form_d":    form_d,
    }


def handle_action(action: str, payload: dict) -> object:
    if action == "all_data":
        return build_all_data(
            days_back_fd=payload.get("days_back_fd", 30),
            days_back_ipo=payload.get("days_back_ipo", 90),
        )

    if action == "form_d_filings":
        days = payload.get("days_back", 7)
        return fetch_form_d(days_back=days)

    if action == "ipo_pipeline":
        days = payload.get("days_back", 90)
        return fetch_ipo_pipeline(days_back=days)

    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    import sys
    action = sys.argv[1] if len(sys.argv) > 1 else "form_d_filings"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    result = handle_action(action, payload)
    print(json.dumps(result, indent=2, default=str))
