"""
Senate / House Financial Disclosures — Periodic Transaction Reports (PTR)
Real congressional trade data under the STOCK Act (2012).

Sources (in priority order):
  1. Senate PTRs: https://efts.senate.gov/LATEST/search-index  (no key)
  2. House FDS:   https://disclosures-clerk.house.gov/public_disc/financial-pdfs/<yr>FD.zip (no key)
  3. Finnhub API: https://finnhub.io/api/v1/stock/congressional-trading (free, 60 req/min)
     Set FINNHUB_API_KEY env var for Option C fallback (free key at finnhub.io)
  Member roster:
    Congress.gov API — set CONGRESS_GOV_API_KEY env var (free at api.congress.gov)
    Senate.gov JSON  — https://www.senate.gov/senators/senators-contact.json (no key fallback)

Actions (PythonRunner):
  all_data          — {members, trades} for PowerTraderService.parse_summary()
  senate_ptrs       — raw Senate PTR filing list
  house_ptrs        — raw House FD PTR filing list
  congress_members  — full live member roster [{id,full_name,party,state,chamber,committees}]
  compute_signal_scores — score a list of trades
"""

import json
import sys
import os
import re
import io
import time
import math
import zipfile
import xml.etree.ElementTree as ET
from datetime import date, timedelta, datetime
from typing import Optional, Dict, List, Tuple

try:
    import requests
except ImportError:
    requests = None


# ── API keys / endpoints ──────────────────────────────────────────────────────

CONGRESS_GOV_API_KEY = os.environ.get('CONGRESS_GOV_API_KEY', '')
CONGRESS_GOV_BASE    = "https://api.congress.gov/v3"
SENATE_CONTACT_JSON  = "https://www.senate.gov/senators/senators-contact.json"
SENATE_EFTS          = "https://efts.senate.gov/LATEST/search-index"
HOUSE_FDS_BASE       = "https://disclosures-clerk.house.gov"

CURRENT_CONGRESS     = 119   # 119th Congress (2025-2027)

# ── Sector / committee maps ───────────────────────────────────────────────────

COMMITTEE_SECTOR_MAP: Dict[str, List[str]] = {
    "Armed Services":               ["Defense", "Aerospace"],
    "Veterans Affairs":             ["Defense", "Healthcare"],
    "Intelligence":                 ["Defense", "Cybersecurity", "Technology"],
    "Finance":                      ["Financials", "Banking"],
    "Banking":                      ["Financials", "Banking", "Insurance"],
    "Banking, Housing, and Urban Affairs": ["Financials", "Banking"],
    "Commerce":                     ["Technology", "Telecom", "Consumer"],
    "Commerce, Science, and Transportation": ["Technology", "Telecom"],
    "Energy and Natural Resources": ["Energy", "Mining", "Utilities"],
    "Health":                       ["Healthcare", "Biotech", "Pharma"],
    "Health, Education, Labor, and Pensions": ["Healthcare", "Biotech"],
    "Agriculture":                  ["Agriculture", "Food"],
    "Agriculture, Nutrition, and Forestry": ["Agriculture"],
    "Foreign Relations":            ["Defense", "International"],
    "Judiciary":                    ["Legal", "Tech (regulatory)"],
    "Appropriations":               ["Defense", "Healthcare", "Infrastructure"],
    "Financial Services":           ["Financials", "Banking"],
    "Energy and Commerce":          ["Energy", "Technology", "Telecom"],
    "Homeland Security":            ["Defense", "Cybersecurity"],
    "Ways and Means":               ["Financials", "Tax"],
    "Rules":                        [],
    "Budget":                       ["Financials"],
    "Environment":                  ["Energy"],
    "Foreign Affairs":              ["Defense", "International"],
    "Education":                    ["Consumer"],
    "Natural Resources":            ["Energy", "Mining"],
}

SECTOR_TICKERS: Dict[str, List[str]] = {
    "Defense":      ["LMT", "RTX", "NOC", "BA", "GD", "LDOS", "SAIC", "HII", "AXON"],
    "Aerospace":    ["BA", "RKLB", "ASTR", "SPCE", "RHM"],
    "Technology":   ["AAPL", "MSFT", "GOOGL", "AMZN", "META", "NVDA", "AMD",
                     "QCOM", "TSM", "CRM", "ORCL", "INTC", "AVGO", "TSLA"],
    "Cybersecurity":["PANW", "CRWD", "PLTR", "FTNT", "ZS", "OKTA", "S", "CYBR"],
    "Financials":   ["JPM", "BAC", "GS", "MS", "WFC", "C", "BX", "KKR", "AXP",
                     "V", "MA", "SPGI", "ICE", "SCHW", "BLK", "COF", "USB"],
    "Healthcare":   ["JNJ", "UNH", "CVS", "HUM", "ABT", "ABBV", "MRK", "PFE",
                     "ELV", "MCK", "CAH"],
    "Biotech":      ["MRNA", "BNTX", "REGN", "GILD", "BIIB", "VRTX", "ALNY"],
    "Energy":       ["XOM", "CVX", "COP", "EOG", "SLB", "HAL", "OXY",
                     "PSX", "PXD", "NEE", "DUK", "SO", "D"],
    "Telecom":      ["VZ", "T", "TMUS", "CHTR", "CMCSA"],
    "Consumer":     ["AMZN", "DIS", "CMCSA", "NFLX", "SBUX", "COST", "HD", "LOW", "TGT"],
    "Industrials":  ["CAT", "DE", "HON", "UNP", "UPS", "FDX", "WM", "EMR"],
    "Mining":       ["NEM", "FCX", "BHP", "RIO", "VALE", "X", "CLF"],
    "Agriculture":  ["ADM", "BG", "MOS", "NTR", "CF"],
}

# Pre-computed: ticker → the committee most likely to have insider knowledge of it
def _build_ticker_committee_map() -> Dict[str, str]:
    """Reverse-map: ticker → committee with oversight of that sector."""
    # sector → first matching committee
    sector_to_cmte: Dict[str, str] = {}
    for cmte, sectors in COMMITTEE_SECTOR_MAP.items():
        for sec in sectors:
            if sec not in sector_to_cmte:
                sector_to_cmte[sec] = cmte
    result: Dict[str, str] = {}
    for sec, tickers in SECTOR_TICKERS.items():
        cmte = sector_to_cmte.get(sec, "")
        if cmte:
            for t in tickers:
                if t not in result:
                    result[t] = cmte
    return result

TICKER_COMMITTEE_MAP = _build_ticker_committee_map()

# Fallback member data (used when all live sources fail)
KNOWN_MEMBERS: Dict[str, Dict] = {
    "tommy tuberville":       {"party": "R", "state": "AL", "chamber": "Senate",
                               "committees": ["Armed Services", "Agriculture, Nutrition, and Forestry"]},
    "mark kelly":             {"party": "D", "state": "AZ", "chamber": "Senate",
                               "committees": ["Armed Services", "Commerce, Science, and Transportation"]},
    "tom cotton":             {"party": "R", "state": "AR", "chamber": "Senate",
                               "committees": ["Armed Services", "Intelligence"]},
    "john cornyn":            {"party": "R", "state": "TX", "chamber": "Senate",
                               "committees": ["Intelligence", "Finance"]},
    "maria cantwell":         {"party": "D", "state": "WA", "chamber": "Senate",
                               "committees": ["Commerce, Science, and Transportation", "Finance", "Energy and Natural Resources"]},
    "chuck schumer":          {"party": "D", "state": "NY", "chamber": "Senate",
                               "committees": ["Finance", "Rules", "Judiciary"]},
    "mitch mcconnell":        {"party": "R", "state": "KY", "chamber": "Senate",
                               "committees": ["Appropriations", "Rules"]},
    "rick scott":             {"party": "R", "state": "FL", "chamber": "Senate",
                               "committees": ["Commerce, Science, and Transportation", "Armed Services", "Budget"]},
    "lisa murkowski":         {"party": "R", "state": "AK", "chamber": "Senate",
                               "committees": ["Appropriations", "Energy and Natural Resources", "Health, Education, Labor, and Pensions"]},
    "shelley moore capito":   {"party": "R", "state": "WV", "chamber": "Senate",
                               "committees": ["Appropriations", "Environment"]},
    "elizabeth warren":       {"party": "D", "state": "MA", "chamber": "Senate",
                               "committees": ["Banking, Housing, and Urban Affairs", "Finance", "Armed Services"]},
    "jon ossoff":             {"party": "D", "state": "GA", "chamber": "Senate",
                               "committees": ["Banking, Housing, and Urban Affairs", "Intelligence"]},
    "kirsten gillibrand":     {"party": "D", "state": "NY", "chamber": "Senate",
                               "committees": ["Armed Services", "Finance"]},
    "bill hagerty":           {"party": "R", "state": "TN", "chamber": "Senate",
                               "committees": ["Banking, Housing, and Urban Affairs", "Foreign Relations", "Appropriations"]},
    "roger wicker":           {"party": "R", "state": "MS", "chamber": "Senate",
                               "committees": ["Armed Services", "Commerce, Science, and Transportation"]},
    "thom tillis":            {"party": "R", "state": "NC", "chamber": "Senate",
                               "committees": ["Banking, Housing, and Urban Affairs", "Judiciary", "Armed Services"]},
    "ted cruz":               {"party": "R", "state": "TX", "chamber": "Senate",
                               "committees": ["Commerce, Science, and Transportation", "Judiciary", "Foreign Relations"]},
    "marco rubio":            {"party": "R", "state": "FL", "chamber": "Senate",
                               "committees": ["Intelligence", "Foreign Relations"]},
    "pat toomey":             {"party": "R", "state": "PA", "chamber": "Senate",
                               "committees": ["Banking, Housing, and Urban Affairs", "Finance", "Budget"]},
    "susan collins":          {"party": "R", "state": "ME", "chamber": "Senate",
                               "committees": ["Appropriations", "Finance", "Health, Education, Labor, and Pensions"]},
    "ron johnson":            {"party": "R", "state": "WI", "chamber": "Senate",
                               "committees": ["Homeland Security", "Finance"]},
    "nancy pelosi":           {"party": "D", "state": "CA", "chamber": "House",
                               "committees": ["Financial Services", "Ways and Means"]},
    "dan crenshaw":           {"party": "R", "state": "TX", "chamber": "House",
                               "committees": ["Homeland Security", "Energy and Commerce"]},
    "michael mccaul":         {"party": "R", "state": "TX", "chamber": "House",
                               "committees": ["Foreign Affairs", "Homeland Security"]},
    "josh gottheimer":        {"party": "D", "state": "NJ", "chamber": "House",
                               "committees": ["Financial Services"]},
    "virginia foxx":          {"party": "R", "state": "NC", "chamber": "House",
                               "committees": ["Education", "Rules"]},
    "greg meeks":             {"party": "D", "state": "NY", "chamber": "House",
                               "committees": ["Financial Services", "Foreign Affairs"]},
    "pete sessions":          {"party": "R", "state": "TX", "chamber": "House",
                               "committees": ["Rules", "Homeland Security"]},
    "jim himes":              {"party": "D", "state": "CT", "chamber": "House",
                               "committees": ["Financial Services", "Intelligence"]},
    "ro khanna":              {"party": "D", "state": "CA", "chamber": "House",
                               "committees": ["Armed Services", "Commerce, Science, and Transportation"]},
    "mike gallagher":         {"party": "R", "state": "WI", "chamber": "House",
                               "committees": ["Armed Services", "Intelligence"]},
}


# ── HTTP helper ───────────────────────────────────────────────────────────────

def _get(url: str, params: dict = None, timeout: int = 8) -> dict:
    """HTTP GET with a short timeout so the script never hangs the UI."""
    if requests is None:
        return {"error": "requests not available"}
    try:
        r = requests.get(
            url, params=params, timeout=timeout,
            headers={"User-Agent": "FinceptTerminal/1.0 (financial research)"}
        )
        r.raise_for_status()
        return r.json()
    except Exception as e:
        return {"error": str(e)}


# ── Parsing helpers ───────────────────────────────────────────────────────────

def _parse_amount_range(label: str) -> Tuple[float, float]:
    if not label:
        return 0.0, 0.0
    clean = re.sub(r"[$,]", "", label)
    parts = re.split(r"\s*[-–]\s*", clean)
    try:
        low  = float(re.sub(r"[^0-9.]", "", parts[0])) if parts else 0.0
        high = float(re.sub(r"[^0-9.]", "", parts[1])) if len(parts) > 1 else low
        return low, high
    except (ValueError, IndexError):
        return 0.0, 0.0


def _compute_lag(tx_date_str: str, filed_date_str: str) -> int:
    try:
        tx    = datetime.strptime(tx_date_str[:10], "%Y-%m-%d").date()
        filed = datetime.strptime(filed_date_str[:10], "%Y-%m-%d").date()
        return max(0, (filed - tx).days)
    except Exception:
        return 0


def _map_direction(raw: str) -> str:
    r = (raw or "").lower()
    if "purchase" in r or "buy" in r:   return "Buy"
    if "sale" in r or "sell" in r:      return "Sell"
    if "exchange" in r:                 return "Exchange"
    return "Other"


def _map_asset_type(raw: str) -> str:
    r = (raw or "").lower()
    if "option" in r:                           return "Option"
    if "etf" in r or "exchange-traded" in r:   return "ETF"
    if "bond" in r or "note" in r or "treasury" in r or "debenture" in r:
        return "Bond"
    if "mutual" in r:                          return "MutualFund"
    return "Stock"


def _make_member_id(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def _compute_committee_relevance(ticker: str, committees: List[str]) -> str:
    """Return the committee name if this ticker falls under any of the member's committees."""
    cmte_for_ticker = TICKER_COMMITTEE_MAP.get(ticker.upper(), "")
    if not cmte_for_ticker:
        return ""
    for c in committees:
        # Partial match — committee names often shorten (e.g. "Banking" ≈ "Banking, Housing…")
        if cmte_for_ticker.lower()[:8] in c.lower() or c.lower()[:8] in cmte_for_ticker.lower():
            return c
    return ""


# ── Dynamic member roster ─────────────────────────────────────────────────────

_roster_cache: Dict[str, Dict] = {}   # name.lower() → member dict


def _load_roster() -> Dict[str, Dict]:
    """
    Build a name-keyed member lookup.
    Priority: Congress.gov API → Senate.gov JSON → KNOWN_MEMBERS.
    Result is module-level cached for the process lifetime.
    """
    global _roster_cache
    if _roster_cache:
        return _roster_cache

    members = _fetch_congress_gov_members() or _fetch_senate_gov_members()

    if members:
        for m in members:
            key = m.get("full_name", "").lower().strip()
            if key:
                _roster_cache[key] = m
    else:
        # Fallback: use KNOWN_MEMBERS directly
        for name, v in KNOWN_MEMBERS.items():
            _roster_cache[name] = {
                "id":        _make_member_id(name),
                "full_name": " ".join(w.capitalize() for w in name.split()),
                "party":     v["party"],
                "state":     v["state"],
                "chamber":   v["chamber"],
                "committees":v["committees"],
            }

    return _roster_cache


def _fetch_congress_gov_members() -> List[Dict]:
    """Fetch 119th Congress member roster via Congress.gov API (requires API key)."""
    if not CONGRESS_GOV_API_KEY:
        return []
    members = []
    for chamber_param in ["Senate", "House"]:
        url = (f"{CONGRESS_GOV_BASE}/member"
               f"?congress={CURRENT_CONGRESS}&chamber={chamber_param}"
               f"&format=json&limit=250&api_key={CONGRESS_GOV_API_KEY}")
        data = _get(url)
        if "error" in data:
            continue
        for m in data.get("members", []):
            raw_name = m.get("name", "")
            # "Last, First Middle" → "First Middle Last"
            if "," in raw_name:
                parts = raw_name.split(",", 1)
                full_name = f"{parts[1].strip()} {parts[0].strip()}"
            else:
                full_name = raw_name
            party_raw = m.get("partyName", m.get("party", ""))
            party = ("D" if "democrat" in party_raw.lower()
                     else "R" if "republican" in party_raw.lower()
                     else "I")
            state_raw = m.get("state", "")
            state = state_raw[:2].upper() if state_raw else ""
            member_id = _make_member_id(full_name)
            # Committees not available in list endpoint — use KNOWN_MEMBERS lookup
            known = KNOWN_MEMBERS.get(full_name.lower(), {})
            members.append({
                "id":          member_id,
                "full_name":   full_name,
                "party":       party,
                "state":       state,
                "chamber":     chamber_param,
                "committees":  known.get("committees", []),
                "bioguide_id": m.get("bioguideId", ""),
            })
    return members


def _fetch_senate_gov_members() -> List[Dict]:
    """
    Fetch Senate roster from senate.gov (no API key needed).
    Returns only senators; House members fall back to KNOWN_MEMBERS.
    """
    data = _get(SENATE_CONTACT_JSON, timeout=8)
    if "error" in data or not isinstance(data, list):
        return []
    members = []
    for senator in data:
        name_obj = senator.get("name", {})
        if isinstance(name_obj, dict):
            first = name_obj.get("first", "")
            last  = name_obj.get("last", "")
        else:
            first, last = "", str(name_obj)
        full_name = f"{first} {last}".strip()
        if not full_name:
            continue
        state = senator.get("state", "")[:2].upper()
        party = senator.get("party", "").upper()[:1]
        if party not in ("D", "R", "I"):
            party = ""
        known = KNOWN_MEMBERS.get(full_name.lower(), {})
        members.append({
            "id":         _make_member_id(full_name),
            "full_name":  full_name,
            "party":      party,
            "state":      state,
            "chamber":    "Senate",
            "committees": known.get("committees", []),
        })
    # Supplement with House from KNOWN_MEMBERS
    for name, v in KNOWN_MEMBERS.items():
        if v["chamber"] == "House":
            members.append({
                "id":         _make_member_id(name),
                "full_name":  " ".join(w.capitalize() for w in name.split()),
                "party":      v["party"],
                "state":      v["state"],
                "chamber":    "House",
                "committees": v["committees"],
            })
    return members


def _enrich_member(name: str, chamber_hint: str = "senate") -> Dict:
    """Look up a member by name in the live roster (falls back to KNOWN_MEMBERS)."""
    roster = _load_roster()
    key = name.lower().strip()
    if key in roster:
        return roster[key]
    # Last-name partial match
    parts = key.split()
    if len(parts) >= 2:
        last = parts[-1]
        for k, v in roster.items():
            if k.split()[-1] == last:
                return v
    default = "Senate" if "senate" in chamber_hint.lower() else "House"
    return {"party": "", "state": "", "chamber": default, "committees": []}


# ── Senate eFTS ────────────────────────────────────────────────────────────────

def fetch_senate_ptrs(days_back: int = 90) -> list:
    """
    Fetch Senate PTR filings from eFTS. Returns a list of filing dicts,
    each containing a 'trades' list.
    """
    start  = (date.today() - timedelta(days=days_back)).isoformat()
    today  = date.today().isoformat()
    result = []
    offset = 0
    limit  = 100

    while True:
        params = {
            "q":         "",
            "dateRange": "custom",
            "fromDate":  start,
            "toDate":    today,
            "limit":     limit,
            "offset":    offset,
        }
        data = _get(SENATE_EFTS, params)
        if "error" in data:
            break
        hits_obj = data.get("hits", {})
        hits     = hits_obj.get("hits", [])
        if not hits:
            break

        for hit in hits:
            src          = hit.get("_source", {})
            report_types = [rt.lower() for rt in src.get("report_types", [])]
            if not any("ptr" in rt for rt in report_types):
                continue
            filing = _parse_efts_hit(hit.get("_id", ""), src)
            if filing:
                result.append(filing)

        total_val = (hits_obj.get("total", {}).get("value", 0)
                     if isinstance(hits_obj.get("total"), dict)
                     else int(hits_obj.get("total") or 0))
        offset += limit
        if offset >= total_val or len(hits) < limit:
            break

    return result


def _parse_efts_hit(filing_id: str, src: dict) -> Optional[dict]:
    first = src.get("first_name", "")
    last  = src.get("last_name", "")
    member_name = f"{first} {last}".strip() or "Unknown"
    filed_date  = src.get("date_received", src.get("filed", ""))
    filing_url  = src.get("link", "")

    raw_txns = src.get("transactions", [])
    if not raw_txns and filing_id:
        raw_txns = _fetch_senate_filing_transactions(filing_id)
    if not raw_txns:
        return None

    enrich     = _enrich_member(member_name, "senate")
    committees = enrich.get("committees", [])

    skip_types = {"trust", "retirement", "529", "ira", "savings bond",
                  "pension", "annuity", "checking", "money market"}

    trades = []
    for txn in raw_txns:
        asset_name = txn.get("asset_name", txn.get("assetName", ""))
        ticker     = txn.get("ticker", "").strip().upper()
        raw_type   = txn.get("type", txn.get("transaction_type", ""))
        amount_str = txn.get("amount", "")
        tx_date    = txn.get("transaction_date", txn.get("date", ""))
        asset_type = txn.get("asset_type", txn.get("assetType", "Stock"))

        if any(s in (asset_type or "").lower() for s in skip_types):
            continue

        lo, hi    = _parse_amount_range(amount_str)
        direction = _map_direction(raw_type)
        at_mapped = _map_asset_type(asset_type)
        tx_iso    = tx_date[:10] if tx_date else ""
        cmte_rel  = _compute_committee_relevance(ticker, committees) if ticker else ""

        trades.append({
            "ticker":             ticker,
            "asset_name":         asset_name,
            "asset_type":         at_mapped,
            "direction":          direction,
            "transaction_date":   tx_iso,
            "amount_low":         lo,
            "amount_high":        hi,
            "amount_range_label": amount_str,
            "committee_relevance": cmte_rel,
        })

    if not trades:
        return None

    first_tx = trades[0].get("transaction_date", "")
    lag      = _compute_lag(first_tx, filed_date) if first_tx and filed_date else 0

    return {
        "member_name":         member_name,
        "chamber":             "senate",
        "party":               enrich.get("party", ""),
        "state":               enrich.get("state", ""),
        "committees":          committees,
        "filed_date":          filed_date,
        "disclosure_lag_days": lag,
        "trades":              trades,
        "filing_url":          filing_url,
        "data_source":         "Senate eFTS",
    }


def _fetch_senate_filing_transactions(filing_id: str) -> list:
    """Re-query eFTS by filing ID to retrieve embedded transactions."""
    if not filing_id or requests is None:
        return []
    try:
        data = _get(SENATE_EFTS, {"q": f'"{filing_id}"'}, timeout=10)
        for hit in data.get("hits", {}).get("hits", []):
            txns = hit.get("_source", {}).get("transactions", [])
            if txns:
                return txns
    except Exception:
        pass
    return []


# ── House FDS ──────────────────────────────────────────────────────────────────

def fetch_house_ptrs(days_back: int = 90) -> list:
    """Fetch House PTRs from the annual ZIP at disclosures-clerk.house.gov."""
    if requests is None:
        return []
    cutoff  = date.today() - timedelta(days=days_back)
    results = []
    for yr in [date.today().year, date.today().year - 1]:
        filings = _parse_house_fd_zip(yr, cutoff)
        results.extend(filings)
        if results:
            break
    return results


def _parse_house_fd_zip(year: int, cutoff: date) -> list:
    cache_dir  = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "__pycache__", "house_fd")
    os.makedirs(cache_dir, exist_ok=True)
    cache_file = os.path.join(cache_dir, f"{year}FD.zip")
    zip_url    = f"{HOUSE_FDS_BASE}/public_disc/financial-pdfs/{year}FD.zip"
    use_cache  = (os.path.exists(cache_file)
                  and (time.time() - os.path.getmtime(cache_file)) < 6 * 3600)
    try:
        if use_cache:
            with open(cache_file, "rb") as f:
                zip_data = f.read()
        else:
            resp = requests.get(zip_url, timeout=10,
                                headers={"User-Agent": "FinceptTerminal/1.0"})
            if resp.status_code != 200:
                return []
            zip_data = resp.content
            with open(cache_file, "wb") as f:
                f.write(zip_data)

        filings = []
        with zipfile.ZipFile(io.BytesIO(zip_data)) as zf:
            for name in zf.namelist():
                if not name.lower().endswith(".xml"):
                    continue
                try:
                    filing = _parse_house_xml(zf.read(name), cutoff)
                    if filing:
                        filings.append(filing)
                except Exception:
                    continue
        return filings
    except Exception:
        return []


def _parse_house_xml(xml_bytes: bytes, cutoff: date) -> Optional[dict]:
    try:
        root = ET.fromstring(xml_bytes)
        member_el = root.find("Member")
        if member_el is None:
            member_el = root.find("member")
        if member_el is None:
            return None

        filing_type = (member_el.findtext("FilingType") or "").strip().upper()
        if filing_type not in ("P", "PTR"):
            return None

        first     = (member_el.findtext("First")         or "").strip()
        last      = (member_el.findtext("Last")          or "").strip()
        party     = (member_el.findtext("Party")         or "").strip()
        state_str = (member_el.findtext("StateDistrict") or "").strip()
        state     = state_str[:2] if state_str else ""
        filed_raw = (member_el.findtext("FilingDate")    or "").strip()

        try:
            filed_date = datetime.strptime(filed_raw, "%m/%d/%Y").date()
        except Exception:
            filed_date = date.today()

        if filed_date < cutoff:
            return None

        member_name = f"{first} {last}".strip()
        if not member_name:
            return None

        txns_el = root.find("Transactions")
        if txns_el is None:
            txns_el = root.find("transactions")
        if txns_el is None:
            return None

        enrich     = _enrich_member(member_name, "house")
        committees = enrich.get("committees", [])
        trades     = []

        for txn in txns_el:
            tx_date_raw = (txn.findtext("TransactionDate") or "").strip()
            ticker      = (txn.findtext("Ticker")          or "").strip().upper()
            asset_name  = (txn.findtext("AssetName")       or "").strip()
            asset_type  = (txn.findtext("AssetType")       or "ST").strip()
            tx_type     = (txn.findtext("Type")            or "").strip()
            amount_str  = (txn.findtext("Amount")          or "").strip()

            if not (asset_name or ticker):
                continue
            try:
                tx_iso = datetime.strptime(tx_date_raw, "%m/%d/%Y").strftime("%Y-%m-%d")
            except Exception:
                tx_iso = filed_date.isoformat()

            lo, hi   = _parse_amount_range(amount_str)
            cmte_rel = _compute_committee_relevance(ticker, committees) if ticker else ""

            trades.append({
                "ticker":             ticker,
                "asset_name":         asset_name,
                "asset_type":         _map_asset_type(asset_type),
                "direction":          _map_direction(tx_type),
                "transaction_date":   tx_iso,
                "amount_low":         lo,
                "amount_high":        hi,
                "amount_range_label": amount_str,
                "committee_relevance": cmte_rel,
            })

        if not trades:
            return None

        lag = _compute_lag(trades[0]["transaction_date"], filed_date.isoformat())
        return {
            "member_name":         member_name,
            "chamber":             "house",
            "party":               party or enrich.get("party", ""),
            "state":               state  or enrich.get("state", ""),
            "committees":          committees,
            "filed_date":          filed_date.isoformat(),
            "disclosure_lag_days": lag,
            "trades":              trades,
            "filing_url":          "",
            "data_source":         "House FDS",
        }
    except Exception:
        return None


# ── Signal scoring ────────────────────────────────────────────────────────────

def compute_signal_score(trade: dict, member: dict) -> float:
    score       = 0.0
    ticker      = trade.get("ticker", "")
    committees  = member.get("committees", [])
    amount_high = trade.get("amount_high", 0)
    cmte_rel    = trade.get("committee_relevance", "")

    # Committee relevance (up to 30 points)
    if cmte_rel:
        score += 30
    else:
        # Partial check via ticker→committee map
        cmte_for_ticker = TICKER_COMMITTEE_MAP.get(ticker.upper(), "")
        if cmte_for_ticker and any(cmte_for_ticker[:8].lower() in c.lower() for c in committees):
            score += 20

    # Disclosure lag (up to 15 points) — longer lag = more suspicious
    lag = trade.get("disclosure_lag_days", 0)
    if lag > 40:    score += 15
    elif lag > 30:  score += 8
    elif lag > 20:  score += 3

    # Trade size (up to 10 points)
    if amount_high >= 1_000_000:   score += 10
    elif amount_high >= 250_000:   score += 5
    elif amount_high >= 50_000:    score += 2

    return min(100.0, score)


# ── Full summary builder ───────────────────────────────────────────────────────

def _network_available() -> bool:
    """Quick DNS probe — returns False within ~3s if offline."""
    if requests is None:
        return False
    try:
        requests.head("https://efts.senate.gov", timeout=3)
        return True
    except Exception:
        return False


# ── Finnhub congressional fallback (Option C) ─────────────────────────────────
# Used when Senate eFTS and House FDS both return nothing (e.g. network issues,
# no PTR filings in date range). Finnhub free tier: 60 req/min, no sign-up barrier.
# Set FINNHUB_API_KEY env var; without a key this is skipped gracefully.

FINNHUB_API_KEY = os.environ.get('FINNHUB_API_KEY', '')
FINNHUB_CONGRESSIONAL = "https://finnhub.io/api/v1/stock/congressional-trading"

# Top tickers most commonly traded by Congress members (sourced from research).
# Querying these 30 symbols covers the majority of high-signal congressional trades.
_CONGRESSIONAL_WATCHLIST = [
    "AAPL", "MSFT", "NVDA", "AMZN", "GOOGL", "META", "TSLA", "AMD",
    "JPM", "BAC", "GS", "V", "MA", "BX", "SCHW",
    "LMT", "RTX", "NOC", "BA", "GD", "PLTR", "LDOS",
    "XOM", "CVX", "COP", "NEE",
    "UNH", "JNJ", "PFE", "ABBV",
]


def _fetch_finnhub_congressional(days_back: int = 90) -> list:
    """
    Fetch congressional trades from Finnhub for the top watchlist tickers.
    Returns filings in the same format as fetch_senate_ptrs() so they can be
    merged directly into build_all_data().
    Requires FINNHUB_API_KEY environment variable.
    """
    if not FINNHUB_API_KEY or requests is None:
        return []

    start = (date.today() - timedelta(days=days_back)).isoformat()
    end   = date.today().isoformat()
    filings: List[dict] = []
    seen_ids: set = set()

    for ticker in _CONGRESSIONAL_WATCHLIST:
        try:
            r = requests.get(
                FINNHUB_CONGRESSIONAL,
                params={"symbol": ticker, "from": start, "to": end,
                        "token": FINNHUB_API_KEY},
                timeout=8,
                headers={"User-Agent": "FinceptTerminal/1.0"}
            )
            r.raise_for_status()
            data = r.json()
        except Exception:
            continue

        for trade in data.get("data", []):
            name = trade.get("name", "").strip()
            if not name:
                continue

            tx_date  = trade.get("transactionDate", "")
            fil_date = trade.get("filingDate",      "")
            amount   = trade.get("amount", 0) or 0
            tx_type  = (trade.get("transaction", "") or "").lower()
            comment  = trade.get("comment", "") or ""

            # Deduplicate by natural key
            key = f"{name}|{ticker}|{tx_date}|{amount}"
            if key in seen_ids:
                continue
            seen_ids.add(key)

            direction = "Buy" if "purchase" in tx_type or "buy" in tx_type else \
                        "Sell" if "sale" in tx_type or "sell" in tx_type else "Other"

            # Estimate amount range bracket from midpoint
            lo = hi = float(amount)
            if   amount >= 1_000_000: lo, hi, lbl = 500_001, 1_000_000, "$500,001 – $1,000,000"
            elif amount >= 500_000:   lo, hi, lbl = 250_001,   500_000, "$250,001 – $500,000"
            elif amount >= 250_000:   lo, hi, lbl = 100_001,   250_000, "$100,001 – $250,000"
            elif amount >= 100_000:   lo, hi, lbl =  50_001,   100_000, "$50,001 – $100,000"
            elif amount >= 50_000:    lo, hi, lbl =  15_001,    50_000, "$15,001 – $50,000"
            elif amount >= 15_000:    lo, hi, lbl =   1_001,    15_000, "$1,001 – $15,000"
            else:                     lbl = f"~${int(amount):,}"

            # Compute lag
            try:
                lag = max(0, (date.fromisoformat(fil_date[:10])
                              - date.fromisoformat(tx_date[:10])).days)
            except Exception:
                lag = 0

            enrich   = _enrich_member(name, "")
            cmte_rel = _compute_committee_relevance(ticker, enrich.get("committees", []))
            score    = compute_signal_score(
                {"ticker": ticker, "amount_high": hi,
                 "disclosure_lag_days": lag, "committee_relevance": cmte_rel},
                {"committees": enrich.get("committees", [])})

            filings.append({
                "member_name":         name,
                "chamber":             "senate" if enrich.get("chamber","").lower() == "senate" else "house",
                "party":               enrich.get("party", ""),
                "state":               enrich.get("state", ""),
                "committees":          enrich.get("committees", []),
                "filed_date":          fil_date[:10] if fil_date else "",
                "disclosure_lag_days": lag,
                "data_source":         "Finnhub",
                "filing_url":          "",
                "trades": [{
                    "ticker":             ticker,
                    "asset_name":         trade.get("asset", ticker),
                    "asset_type":         "Stock",
                    "direction":          direction,
                    "transaction_date":   tx_date[:10] if tx_date else "",
                    "amount_low":         float(lo),
                    "amount_high":        float(hi),
                    "amount_range_label": lbl,
                    "committee_relevance": cmte_rel,
                }],
            })

        time.sleep(1.1)  # stay within 60 req/min free tier (1 sec gap is safe)

    return filings


def _compute_committee_relevance(ticker: str, committees: List[str]) -> str:
    """Thin wrapper used by Finnhub fallback path (TICKER_COMMITTEE_MAP removed)."""
    return _compute_committee_relevance_impl(ticker, committees)


def _compute_committee_relevance_impl(ticker: str, committees: List[str]) -> str:
    cmte_for_ticker = TICKER_COMMITTEE_MAP.get(ticker.upper(), "")
    if not cmte_for_ticker:
        return ""
    for c in committees:
        if cmte_for_ticker.lower()[:8] in c.lower() or c.lower()[:8] in cmte_for_ticker.lower():
            return c
    return ""


def build_all_data(days_back: int = 90) -> dict:
    """
    Build {members, trades} compatible with PowerTraderService.parse_summary().

    Source priority:
      1. Senate eFTS  (primary, authoritative — probed separately)
      2. House FDS    (primary, authoritative)
      3. Finnhub API  (Option C — set FINNHUB_API_KEY env var, tried even when
                       Senate probe fails since Finnhub is a different domain)
      4. Empty result  (C++ caller shows actionable error state)
    """
    # Probe Senate eFTS specifically (not a general internet check).
    # If Senate is unreachable we still try Finnhub (different domain).
    senate_online = _network_available()

    senate_filings: list = []
    house_filings:  list = []

    if senate_online:
        # Pre-load roster only when Senate is reachable (avoids 8s timeout offline)
        _load_roster()
        senate_filings = fetch_senate_ptrs(days_back=days_back)
        house_filings  = fetch_house_ptrs(days_back=days_back)

    all_filings = senate_filings + house_filings

    if not all_filings:
        # Option C: Finnhub is at finnhub.io — reachable even when efts.senate.gov is not.
        # Always attempted when FINNHUB_API_KEY is set, regardless of Senate probe.
        all_filings = _fetch_finnhub_congressional(days_back=days_back)
        if not all_filings:
            return {"members": [], "trades": []}

    member_map: Dict[str, dict] = {}
    trades = []
    trade_idx = 0

    for filing in all_filings:
        member_name = filing.get("member_name", "Unknown")
        chamber_raw = filing.get("chamber", "senate")
        chamber     = "Senate" if chamber_raw == "senate" else "House"
        party       = filing.get("party", "")
        state       = filing.get("state", "")
        committees  = filing.get("committees", [])
        member_id   = _make_member_id(member_name)
        filed_date  = filing.get("filed_date", "")
        lag         = filing.get("disclosure_lag_days", 0)

        if member_id not in member_map:
            member_map[member_id] = {
                "id":                   member_id,
                "full_name":            member_name,
                "party":                party,
                "chamber":              chamber,
                "state":                state,
                "district":             "",
                "committees":           committees,
                "term_start":           "",
                "estimated_net_worth":  0.0,
                "trade_count_ytd":      0,
                "portfolio_return_ytd": 0.0,
                "spy_return_ytd":       0.0,
            }

        mem = member_map[member_id]
        if not mem["party"] and party:
            mem["party"] = party
        if not mem["committees"] and committees:
            mem["committees"] = committees

        for t in filing.get("trades", []):
            trade_idx += 1
            mem["trade_count_ytd"] += 1
            score = compute_signal_score(
                {**t, "disclosure_lag_days": lag},
                {"committees": committees}
            )
            trades.append({
                "id":                  f"trade-{trade_idx:05d}",
                "member_id":           member_id,
                "member_name":         member_name,
                "party":               party,
                "chamber":             chamber,
                "transaction_date":    t.get("transaction_date", ""),
                "disclosure_date":     filed_date,
                "disclosure_lag_days": lag,
                "ticker":              t.get("ticker", ""),
                "asset_name":          t.get("asset_name", ""),
                "amount_low":          t.get("amount_low", 0.0),
                "amount_high":         t.get("amount_high", 0.0),
                "amount_range_label":  t.get("amount_range_label", ""),
                "committee_relevance": t.get("committee_relevance", ""),
                "signal_score":        score,
                "source_url":          filing.get("filing_url", ""),
                "direction":           t.get("direction", "Other"),
                "asset_type":          t.get("asset_type", "Stock"),
                "data_source":         filing.get("data_source", "Senate eFTS"),
            })

    trades.sort(key=lambda x: x.get("disclosure_date", ""), reverse=True)
    return {"members": list(member_map.values()), "trades": trades}

def fetch_congress_members_full() -> list:
    """Return the full live roster as a list, used by the 'congress_members' action."""
    _load_roster()
    return list(_roster_cache.values())


# ── Action handler ────────────────────────────────────────────────────────────

def handle_action(action: str, payload: dict) -> object:
    if action == "all_data":
        return build_all_data(days_back=payload.get("days_back", 90))
    if action == "senate_ptrs":
        return fetch_senate_ptrs(days_back=payload.get("days_back", 30))
    if action == "house_ptrs":
        return fetch_house_ptrs(days_back=payload.get("days_back", 30))
    if action == "congress_members":
        return fetch_congress_members_full()
    if action == "compute_signal_scores":
        members_map = {m["id"]: m for m in payload.get("members", [])}
        trades      = payload.get("trades", [])
        for t in trades:
            t["signal_score"] = compute_signal_score(t, members_map.get(t.get("member_id", ""), {}))
        return trades
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    if len(sys.argv) > 1:
        action  = sys.argv[1]
        payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
        result  = handle_action(action, payload)
        print(json.dumps(result, indent=2, default=str))
