"""
Senate Financial Disclosures — Periodic Transaction Reports (PTR)
Fetches congressional trade disclosures from the Senate eFTS system.

Official API: https://efts.senate.gov/LATEST/search-index
No API key required. Data is public under the STOCK Act (2012).

Actions (daemon):
  senate_ptrs        — recent PTR filings (past N days)
  senate_members     — member roster with committee assignments (via ProPublica)
"""

import json
import sys
import os
import re
import time
from datetime import date, timedelta, datetime

try:
    import requests
except ImportError:
    requests = None


# ── Senate eFTS endpoints ─────────────────────────────────────────────────────

EFTS_BASE = "https://efts.senate.gov/LATEST/search-index"
EFTS_DETAIL = "https://efts.senate.gov/LATEST/search-index?q=\"{filing_id}\""
PTR_PDF_BASE = "https://efts.senate.gov/LATEST/search-index"

# ProPublica Congress API (free, no key required for basic use)
PROPUBLICA_BASE = "https://api.propublica.org/congress/v1"

# ── Committee → sector mapping ────────────────────────────────────────────────

COMMITTEE_SECTOR_MAP = {
    "Armed Services": ["Defense", "Aerospace"],
    "Veterans Affairs": ["Defense", "Healthcare"],
    "Intelligence": ["Defense", "Cybersecurity", "Technology"],
    "Finance": ["Financials", "Banking"],
    "Banking": ["Financials", "Banking", "Insurance"],
    "Commerce": ["Technology", "Telecom", "Consumer"],
    "Energy and Natural Resources": ["Energy", "Mining", "Utilities"],
    "Health": ["Healthcare", "Biotech", "Pharma"],
    "Agriculture": ["Agriculture", "Food"],
    "Foreign Relations": ["Defense", "International"],
    "Judiciary": ["Legal", "Tech (regulatory)"],
    "Appropriations": ["Defense", "Healthcare", "Infrastructure"],
}

SECTOR_TICKERS = {
    "Defense": ["LMT", "RTX", "NOC", "BA", "GD", "L3H"],
    "Technology": ["AAPL", "MSFT", "GOOGL", "AMZN", "META", "NVDA"],
    "Cybersecurity": ["PANW", "CRWD", "PLTR", "FTNT", "ZS"],
    "Financials": ["JPM", "BAC", "GS", "MS", "WFC", "C"],
    "Healthcare": ["JNJ", "UNH", "CVS", "HUM", "ABT"],
    "Biotech": ["MRNA", "BNTX", "REGN", "GILD", "BIIB"],
    "Energy": ["XOM", "CVX", "COP", "EOG", "SLB"],
    "Telecom": ["VZ", "T", "TMUS"],
}


def _get(url: str, params: dict = None, timeout: int = 15) -> dict:
    if requests is None:
        return {"error": "requests not available"}
    try:
        r = requests.get(url, params=params, timeout=timeout,
                         headers={"User-Agent": "FinceptTerminal/1.0 (financial research)"})
        r.raise_for_status()
        return r.json()
    except Exception as e:
        return {"error": str(e)}


def fetch_senate_ptrs(days_back: int = 30) -> list:
    """Fetch recent Senate PTR filings from eFTS."""
    start = (date.today() - timedelta(days=days_back)).isoformat()
    params = {
        "q": "ptr",
        "dateRange": "custom",
        "startdt": start,
        "enddt": date.today().isoformat(),
        "filerType": "senator",
    }
    data = _get(EFTS_BASE, params)
    if "error" in data:
        return []

    hits = data.get("hits", {}).get("hits", [])
    result = []
    for hit in hits:
        src = hit.get("_source", {})
        filing = _parse_ptr_hit(src)
        if filing:
            result.append(filing)
    return result


def _parse_ptr_hit(src: dict) -> dict:
    """Parse a single eFTS PTR hit into a structured trade dict."""
    try:
        transactions = src.get("transactions", [])
        if not transactions:
            return None

        member_name = src.get("name", src.get("filerName", "Unknown"))
        filed_date = src.get("date_received", src.get("filed", ""))

        trades = []
        for txn in transactions:
            asset_name = txn.get("asset_name", txn.get("assetName", ""))
            ticker = txn.get("ticker", "")
            tx_type = txn.get("type", txn.get("transaction_type", ""))
            amount_range = txn.get("amount", "")
            tx_date = txn.get("transaction_date", txn.get("date", ""))
            asset_type = txn.get("asset_type", "Stock")

            direction = "buy"
            if "sale" in tx_type.lower() or "sell" in tx_type.lower():
                direction = "sell"
            elif "exchange" in tx_type.lower():
                direction = "exchange"

            # Parse amount range
            amount_low, amount_high = _parse_amount_range(amount_range)

            trades.append({
                "ticker": ticker.upper() if ticker else "",
                "asset_name": asset_name,
                "asset_type": asset_type,
                "direction": direction,
                "transaction_date": tx_date,
                "amount_low": amount_low,
                "amount_high": amount_high,
                "amount_range_label": amount_range,
            })

        if not trades:
            return None

        return {
            "member_name": member_name,
            "chamber": "senate",
            "filed_date": filed_date,
            "disclosure_lag_days": _compute_lag(trades[0].get("transaction_date", ""), filed_date),
            "trades": trades,
            "filing_url": src.get("link", ""),
        }
    except Exception:
        return None


def _parse_amount_range(label: str) -> tuple:
    """Parse '$15,001 - $50,000' → (15001, 50000)."""
    if not label:
        return 0, 0
    # Strip dollar signs and commas
    clean = re.sub(r"[$,]", "", label)
    parts = re.split(r"\s*[-–]\s*", clean)
    try:
        low = float(re.sub(r"[^0-9.]", "", parts[0])) if parts else 0
        high = float(re.sub(r"[^0-9.]", "", parts[1])) if len(parts) > 1 else low
        return low, high
    except (ValueError, IndexError):
        return 0, 0


def _compute_lag(tx_date_str: str, filed_date_str: str) -> int:
    """Compute days between transaction and disclosure filing."""
    try:
        fmt = "%Y-%m-%d"
        tx = datetime.strptime(tx_date_str[:10], fmt).date()
        filed = datetime.strptime(filed_date_str[:10], fmt).date()
        return max(0, (filed - tx).days)
    except Exception:
        return 0


def fetch_congress_members(chamber: str = "senate") -> list:
    """
    Fetch congress member roster. Uses ProPublica Congress API (free).
    Falls back to a hardcoded list of active members if the API is unavailable.
    """
    data = _get(f"{PROPUBLICA_BASE}/{116}/senate/members.json")
    if "error" in data or "results" not in data:
        return _fallback_members(chamber)

    try:
        members = data["results"][0]["members"]
        result = []
        for m in members:
            result.append({
                "id": m.get("id", ""),
                "full_name": f"{m.get('first_name','')} {m.get('last_name','')}".strip(),
                "party": m.get("party", ""),
                "chamber": chamber,
                "state": m.get("state", ""),
                "district": m.get("district", ""),
                "committees": [],  # ProPublica member endpoint has committees
                "seniority": m.get("seniority", ""),
            })
        return result
    except Exception:
        return _fallback_members(chamber)


def _fallback_members(chamber: str) -> list:
    """Return a hardcoded list of well-known active members for Phase 1."""
    senate = [
        {"id": "pelosi-n", "full_name": "Nancy Pelosi", "party": "D", "state": "CA",
         "committees": ["Appropriations"], "chamber": "house"},
        {"id": "tuberville-t", "full_name": "Tommy Tuberville", "party": "R", "state": "AL",
         "committees": ["Armed Services", "Agriculture"], "chamber": "senate"},
        {"id": "collins-s", "full_name": "Susan Collins", "party": "R", "state": "ME",
         "committees": ["Appropriations", "Health"], "chamber": "senate"},
        {"id": "ossoff-j", "full_name": "Jon Ossoff", "party": "D", "state": "GA",
         "committees": ["Intelligence", "Judiciary"], "chamber": "senate"},
        {"id": "johnson-r", "full_name": "Ron Johnson", "party": "R", "state": "WI",
         "committees": ["Commerce", "Finance"], "chamber": "senate"},
        {"id": "kelly-m", "full_name": "Mark Kelly", "party": "D", "state": "AZ",
         "committees": ["Armed Services", "Commerce"], "chamber": "senate"},
        {"id": "crenshaw-d", "full_name": "Dan Crenshaw", "party": "R", "state": "TX",
         "committees": ["Intelligence", "Homeland Security"], "chamber": "house"},
        {"id": "wicker-r", "full_name": "Roger Wicker", "party": "R", "state": "MS",
         "committees": ["Armed Services", "Commerce"], "chamber": "senate"},
        {"id": "warren-e", "full_name": "Elizabeth Warren", "party": "D", "state": "MA",
         "committees": ["Banking", "Finance", "Armed Services"], "chamber": "senate"},
        {"id": "thune-j", "full_name": "John Thune", "party": "R", "state": "SD",
         "committees": ["Finance", "Commerce"], "chamber": "senate"},
        {"id": "burr-r", "full_name": "Richard Burr", "party": "R", "state": "NC",
         "committees": ["Intelligence", "Finance", "Health"], "chamber": "senate"},
        {"id": "loeffler-k", "full_name": "Kelly Loeffler", "party": "R", "state": "GA",
         "committees": ["Banking", "Health"], "chamber": "senate"},
        {"id": "feinstein-d", "full_name": "Dianne Feinstein", "party": "D", "state": "CA",
         "committees": ["Judiciary", "Intelligence", "Appropriations"], "chamber": "senate"},
        {"id": "inhofe-j", "full_name": "James Inhofe", "party": "R", "state": "OK",
         "committees": ["Armed Services", "Environment"], "chamber": "senate"},
        {"id": "manchin-j", "full_name": "Joe Manchin", "party": "D", "state": "WV",
         "committees": ["Energy", "Armed Services"], "chamber": "senate"},
        {"id": "schumer-c", "full_name": "Chuck Schumer", "party": "D", "state": "NY",
         "committees": ["Finance", "Judiciary", "Rules"], "chamber": "senate"},
        {"id": "mcconnell-m", "full_name": "Mitch McConnell", "party": "R", "state": "KY",
         "committees": ["Appropriations", "Rules"], "chamber": "senate"},
        {"id": "cornyn-j", "full_name": "John Cornyn", "party": "R", "state": "TX",
         "committees": ["Intelligence", "Finance", "Armed Services"], "chamber": "senate"},
        {"id": "scott-r", "full_name": "Rick Scott", "party": "R", "state": "FL",
         "committees": ["Banking", "Commerce", "Armed Services"], "chamber": "senate"},
        {"id": "murkowski-l", "full_name": "Lisa Murkowski", "party": "R", "state": "AK",
         "committees": ["Appropriations", "Energy", "Health"], "chamber": "senate"},
    ]
    return senate


def compute_signal_score(trade: dict, member: dict) -> float:
    """
    Compute a 0-100 signal score for a trade based on:
    - Committee overlap with the stock's sector
    - Disclosure lag (higher = more suspicious)
    - Trade size relative to typical
    """
    score = 0.0
    ticker = trade.get("ticker", "")
    committees = member.get("committees", [])
    amount_high = trade.get("amount_high", 0)

    # Committee overlap (up to 30 points)
    for committee in committees:
        sectors = COMMITTEE_SECTOR_MAP.get(committee, [])
        for sector, tickers in SECTOR_TICKERS.items():
            if sector in sectors and ticker in tickers:
                score += 30
                break

    # Disclosure lag (up to 15 points)
    lag = trade.get("disclosure_lag_days", 0)
    if lag > 40:
        score += 15
    elif lag > 30:
        score += 8
    elif lag > 20:
        score += 3

    # Trade size (up to 10 points)
    if amount_high >= 1_000_000:
        score += 10
    elif amount_high >= 250_000:
        score += 5
    elif amount_high >= 50_000:
        score += 2

    return min(100.0, score)


# ── Daemon action handler ─────────────────────────────────────────────────────

def handle_action(action: str, payload: dict) -> object:
    if action == "senate_ptrs":
        days = payload.get("days_back", 30)
        return fetch_senate_ptrs(days_back=days)

    if action == "congress_members":
        chamber = payload.get("chamber", "senate")
        return fetch_congress_members(chamber=chamber)

    if action == "compute_signal_scores":
        trades = payload.get("trades", [])
        members_map = {m["id"]: m for m in payload.get("members", [])}
        for t in trades:
            member = members_map.get(t.get("member_id", ""), {})
            t["signal_score"] = compute_signal_score(t, member)
        return trades

    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    # CLI test mode
    import sys
    if len(sys.argv) > 1:
        action = sys.argv[1]
        payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
        result = handle_action(action, payload)
        print(json.dumps(result, indent=2, default=str))
