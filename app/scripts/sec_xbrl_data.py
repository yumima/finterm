"""
SEC EDGAR XBRL Data Fetcher
SEC EDGAR XBRL Company Facts: structured financial data — revenue, EPS,
balance sheet for all public US companies (no API key required).
"""
import sys
import json
import os
from typing import Dict, Any, Optional, List
import sec_http  # the one SEC client: contact identity, shared pacing

BASE_URL = "https://data.sec.gov/api/xbrl"


def _make_request(endpoint: str, params: Dict = None) -> Any:
    """SEC JSON, or {"error": ...} saying why not — a refusal or no answer is
    never an empty document."""
    url = f"{BASE_URL}/{endpoint}" if not endpoint.startswith('http') else endpoint
    response, status = sec_http.get(url, timeout=30, params=params, headers={"Accept": "application/json"})
    if response is None:
        return {"error": f"HTTP {status}: {url}" if status else f"Request failed: SEC did not answer for {url}"}
    try:
        return response.json()
    except ValueError as e:
        return {"error": f"JSON decode error: {str(e)}"}


def _format_cik(cik: str) -> str:
    return str(cik).zfill(10)


def get_company_facts(cik: str) -> Any:
    cik_fmt = _format_cik(cik)
    return _make_request(f"companyfacts/CIK{cik_fmt}.json")


def get_company_concept(cik: str, taxonomy: str = "us-gaap",
                         tag: str = "Revenues") -> Any:
    cik_fmt = _format_cik(cik)
    return _make_request(f"companyconcept/CIK{cik_fmt}/{taxonomy}/{tag}.json")


def get_frames(taxonomy: str = "us-gaap", tag: str = "Revenues",
               unit: str = "USD", period: str = "CY2023Q4I") -> Any:
    return _make_request(f"frames/{taxonomy}/{tag}/{unit}/{period}.json")


def get_company_tickers() -> Any:
    """SEC's ticker directory, from sec_directory (the app's one copy)."""
    import sec_directory
    try:
        result = [{"cik": r["cik"], "ticker": r["ticker"], "title": r["title"]} for r in sec_directory.rows()]
        return {"count": len(result), "tickers": result}
    except Exception as e:
        return {"error": f"Failed to fetch tickers: {str(e)}"}


def search_company(name_or_ticker: str) -> Any:
    tickers_data = get_company_tickers()
    if "error" in tickers_data:
        return tickers_data
    query = name_or_ticker.lower()
    matches = []
    for t in tickers_data.get("tickers", []):
        ticker = (t.get("ticker") or "").lower()
        title = (t.get("title") or "").lower()
        if query in ticker or query in title:
            matches.append(t)
        if len(matches) >= 20:
            break
    return {"query": name_or_ticker, "count": len(matches), "results": matches}


def get_recent_filings(cik: str) -> Any:
    cik_fmt = _format_cik(cik)
    data = _make_request(f"https://data.sec.gov/submissions/CIK{cik_fmt}.json")
    if "error" in data:
        return data
    try:
        recent = data.get("filings", {}).get("recent", {})
        filings = []
        forms = recent.get("form", [])
        dates = recent.get("filingDate", [])
        accessions = recent.get("accessionNumber", [])
        descriptions = recent.get("primaryDocument", [])
        for i in range(min(20, len(forms))):
            filings.append({
                "form": forms[i] if i < len(forms) else "",
                "date": dates[i] if i < len(dates) else "",
                "accession": accessions[i] if i < len(accessions) else "",
                "document": descriptions[i] if i < len(descriptions) else ""
            })
        return {
            "cik": cik,
            "name": data.get("name", ""),
            "ticker": data.get("tickers", []),
            "sic": data.get("sic", ""),
            "sic_description": data.get("sicDescription", ""),
            "recent_filings": filings
        }
    except (AttributeError, TypeError) as e:
        return {"error": f"Unexpected submissions document: {str(e)}"}


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if not args:
        print(json.dumps({"error": "No command provided"}))
        return
    command = args[0]
    result = {"error": f"Unknown command: {command}"}
    if command == "facts":
        cik = args[1] if len(args) > 1 else "320193"
        result = get_company_facts(cik)
    elif command == "concept":
        cik = args[1] if len(args) > 1 else "320193"
        taxonomy = args[2] if len(args) > 2 else "us-gaap"
        tag = args[3] if len(args) > 3 else "Revenues"
        result = get_company_concept(cik, taxonomy, tag)
    elif command == "frames":
        taxonomy = args[1] if len(args) > 1 else "us-gaap"
        tag = args[2] if len(args) > 2 else "Revenues"
        unit = args[3] if len(args) > 3 else "USD"
        period = args[4] if len(args) > 4 else "CY2023Q4I"
        result = get_frames(taxonomy, tag, unit, period)
    elif command == "tickers":
        result = get_company_tickers()
    elif command == "search":
        query = args[1] if len(args) > 1 else "Apple"
        result = search_company(query)
    elif command == "filings":
        cik = args[1] if len(args) > 1 else "320193"
        result = get_recent_filings(cik)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
