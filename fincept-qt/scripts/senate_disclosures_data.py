"""
Senate / House Financial Disclosures — Periodic Transaction Reports (PTR)
Real congressional trade data under the STOCK Act (2012).

Sources (in priority order):
  1. Senate eFD:  https://efdsearch.senate.gov/    (no key, but stateful: needs CSRF
                                                    agreement POST + AJAX search)
  2. House FDS:   https://disclosures-clerk.house.gov/public_disc/financial-pdfs/<yr>FD.zip
                                                    (no key, plain ZIP+XML index)
  Member roster + committees:
    Congress.gov API — reads CONGRESS_GOV_API_KEY env var (free at
                       api.congress.gov/sign-up). Qt sets it from SecureStorage
                       via PythonRunner::kManagedCredentialKeys. Without a key,
                       falls back to senate.gov JSON + a small KNOWN_MEMBERS table.
    Committees      — joined from unitedstates/congress-legislators (Congress.gov
                       API doesn't expose member→committee).

Actions (PythonRunner):
  all_data          — {members, trades} for PowerTraderService.parse_summary()
  senate_ptrs       — raw Senate PTR filing list
  house_ptrs        — raw House FD PTR filing list
  congress_members  — full live member roster [{id,full_name,party,state,chamber,committees,...}]
  compute_signal_scores — score a list of trades

NOTE: efts.senate.gov was the legacy endpoint and is permanently unreachable.
      Do not re-introduce it; the canonical Senate source is efdsearch.senate.gov.
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
from concurrent.futures import ThreadPoolExecutor
from datetime import date, timedelta, datetime
from typing import Optional, Dict, List, Tuple
from html.parser import HTMLParser

try:
    import requests
except ImportError:
    requests = None


# ── API keys / endpoints ──────────────────────────────────────────────────────

CONGRESS_GOV_API_KEY = os.environ.get('CONGRESS_GOV_API_KEY', '')
CONGRESS_GOV_BASE    = "https://api.congress.gov/v3"
SENATE_CONTACT_JSON  = "https://www.senate.gov/senators/senators-contact.json"
SENATE_EFD_BASE      = "https://efdsearch.senate.gov"
HOUSE_FDS_BASE       = "https://disclosures-clerk.house.gov"

CURRENT_CONGRESS     = 119   # 119th Congress (2025-2027)

# Senate eFD report type codes (from the eFD search form options)
EFD_REPORT_TYPE_PTR  = 11

# Politeness ceiling for parallel per-PTR HTTP fetches against
# efdsearch.senate.gov. The site has no documented rate limit; 8 is well below
# any reasonable threshold for a government site and well above what we get out
# of serial fetching. Actual worker count is min(this, len(uuids)).
_EFD_PARALLEL_WORKERS = 8

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

# Full state name → 2-letter postal code (Congress.gov member endpoint returns
# the full name, not the abbreviation).
US_STATE_ABBREV: Dict[str, str] = {
    "Alabama":"AL","Alaska":"AK","Arizona":"AZ","Arkansas":"AR","California":"CA",
    "Colorado":"CO","Connecticut":"CT","Delaware":"DE","Florida":"FL","Georgia":"GA",
    "Hawaii":"HI","Idaho":"ID","Illinois":"IL","Indiana":"IN","Iowa":"IA",
    "Kansas":"KS","Kentucky":"KY","Louisiana":"LA","Maine":"ME","Maryland":"MD",
    "Massachusetts":"MA","Michigan":"MI","Minnesota":"MN","Mississippi":"MS","Missouri":"MO",
    "Montana":"MT","Nebraska":"NE","Nevada":"NV","New Hampshire":"NH","New Jersey":"NJ",
    "New Mexico":"NM","New York":"NY","North Carolina":"NC","North Dakota":"ND","Ohio":"OH",
    "Oklahoma":"OK","Oregon":"OR","Pennsylvania":"PA","Rhode Island":"RI","South Carolina":"SC",
    "South Dakota":"SD","Tennessee":"TN","Texas":"TX","Utah":"UT","Vermont":"VT",
    "Virginia":"VA","Washington":"WA","West Virginia":"WV","Wisconsin":"WI","Wyoming":"WY",
    "District of Columbia":"DC","Puerto Rico":"PR","Guam":"GU","American Samoa":"AS",
    "Northern Mariana Islands":"MP","U.S. Virgin Islands":"VI",
}


def _state_to_abbrev(state_raw: str) -> str:
    """Normalize a state name/code to a 2-letter postal code.

    Returns "" on unknown input rather than guessing — silently shipping
    the wrong code (e.g. "WE" for "West Virginia") corrupts downstream
    state-keyed groupings and is worse than missing data.
    """
    if not state_raw:
        return ""
    s = state_raw.strip()
    if len(s) == 2 and s.isalpha():
        return s.upper()
    return US_STATE_ABBREV.get(s, "")


# Pre-computed: ticker → the committee most likely to have insider knowledge of it
def _build_ticker_committee_map() -> Dict[str, str]:
    """Reverse-map: ticker → committee with oversight of that sector."""
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

# Fallback committee assignments — last-resort lookup if Congress.gov API is
# unavailable and the live senate.gov JSON doesn't carry committee data.
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


# ── Cache directory ───────────────────────────────────────────────────────────

_CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "__pycache__", "congressional_data")
os.makedirs(_CACHE_DIR, exist_ok=True)


# ── HTTP helper ───────────────────────────────────────────────────────────────

def _get_json(url: str, params: dict = None, timeout: int = 8) -> dict:
    """HTTP GET returning JSON. Returns {'error': ...} on any failure."""
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
        if cmte_for_ticker.lower()[:8] in c.lower() or c.lower()[:8] in cmte_for_ticker.lower():
            return c
    return ""


def _parse_mdy(s: str) -> Optional[date]:
    """Parse 'MM/DD/YYYY' → date, or None."""
    try:
        return datetime.strptime(s.strip(), "%m/%d/%Y").date()
    except Exception:
        return None


def _iso_from_mdy(s: str) -> str:
    d = _parse_mdy(s)
    return d.isoformat() if d else ""


# ── Senate eFD scraper (efdsearch.senate.gov) ─────────────────────────────────
#
# The Senate's new disclosure portal (replaced the dead efts.senate.gov years
# ago) is a Django app behind an Akamai WAF. To search PTRs we must:
#   1. GET /search/home/  (acquire session cookie + CSRF token)
#   2. POST /search/home/ with the prohibition-agreement flag (sets the
#      "I agree not to use this data for commercial purposes" cookie)
#   3. POST /search/report/data/ — DataTables AJAX returning JSON rows
#   4. For each PTR row, GET /search/view/ptr/{uuid}/ and parse the
#      transaction HTML table
#
# Per-filing transaction parses are cached on disk forever (filings don't
# mutate post-publication). The agreement cookie is short-lived; we just
# re-do the agreement dance every run.

class _PTRTransactionParser(HTMLParser):
    """Pull rows out of the transactions table on a PTR detail page.

    Identifies the correct table by its column-header signature — every PTR's
    transaction table has a header row containing "Transaction Date", "Ticker",
    "Asset Name", and "Amount". This is structural (matches the data we're
    extracting) rather than positional (which table on the page) or textual
    (surrounding chrome), so it survives layout changes.

    Nested tables are handled via a per-depth stack of frames, so an outer
    layout table's <tr>/<td> state cannot bleed into an inner data table's
    rows and vice-versa. The first table whose first row matches the header
    signature wins; subsequent matching tables on the same page are ignored.
    """
    REQUIRED_HEADERS = {"transaction date", "ticker", "asset name", "amount"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack:           List[Dict]        = []  # one frame per open <table>
        self.headers:         List[str]         = []  # headers of the winning table
        self.rows:            List[List[str]]   = []  # body rows of the winning table
        self._target_claimed: bool              = False

    def _top(self) -> Optional[Dict]:
        return self.stack[-1] if self.stack else None

    def handle_starttag(self, tag, attrs):
        if tag == "table":
            self.stack.append({
                "headers":      None,   # filled on this table's first <tr>
                "is_target":    False,  # True iff this table's headers match
                "in_tr":        False,
                "current_row":  [],
                "in_cell":      False,
                "current_cell": [],
            })
            return
        top = self._top()
        if top is None:
            return
        if tag == "tr":
            top["in_tr"] = True
            top["current_row"] = []
        elif tag in ("td", "th") and top["in_tr"]:
            top["in_cell"] = True
            top["current_cell"] = []

    def handle_endtag(self, tag):
        top = self._top()
        if top is None:
            return
        if tag in ("td", "th") and top["in_cell"]:
            top["in_cell"] = False
            cell = " ".join("".join(top["current_cell"]).split())
            top["current_row"].append(cell)
        elif tag == "tr" and top["in_tr"]:
            top["in_tr"] = False
            row = top["current_row"]
            if not row:
                return
            if top["headers"] is None:
                # First row of this table: candidate header row.
                top["headers"] = [c.strip() for c in row]
                lowered = {c.lower() for c in top["headers"]}
                if (not self._target_claimed
                        and self.REQUIRED_HEADERS.issubset(lowered)):
                    top["is_target"] = True
                    self._target_claimed = True
                    self.headers = list(top["headers"])
            elif top["is_target"]:
                self.rows.append([c.strip() for c in row])
        elif tag == "table":
            self.stack.pop()

    def handle_data(self, data):
        top = self._top()
        if top is not None and top["in_cell"]:
            top["current_cell"].append(data)


class SenateEFDScraper:
    """Stateful client for efdsearch.senate.gov."""

    def __init__(self):
        self.session = requests.Session() if requests else None
        if self.session:
            self.session.headers.update({
                "User-Agent": "Mozilla/5.0 (X11; Linux x86_64; rv:124.0) Gecko/20100101 Firefox/124.0",
                "Accept-Language": "en-US,en;q=0.9",
            })
        self.csrf_token: str = ""
        self.agreed: bool    = False
        self.unavailable_reason: str = ""

    # ── private helpers ──────────────────────────────────────────────────────

    def _extract_csrf(self, html: str) -> str:
        m = re.search(r'name="csrfmiddlewaretoken"\s+value="([^"]+)"', html)
        return m.group(1) if m else ""

    def _accept_agreement(self) -> bool:
        if self.session is None:
            self.unavailable_reason = "requests not available"
            return False
        if self.agreed and self.csrf_token:
            return True
        try:
            r = self.session.get(f"{SENATE_EFD_BASE}/search/home/", timeout=12)
            if r.status_code != 200:
                self.unavailable_reason = f"home GET HTTP {r.status_code}"
                return False
            token = self._extract_csrf(r.text)
            if not token:
                self.unavailable_reason = "CSRF token not found on /search/home/"
                return False
            r2 = self.session.post(
                f"{SENATE_EFD_BASE}/search/home/",
                data={
                    "csrfmiddlewaretoken": token,
                    "prohibition_agreement": "1",
                },
                headers={
                    "Referer": f"{SENATE_EFD_BASE}/search/home/",
                    "Origin":  SENATE_EFD_BASE,
                },
                timeout=12, allow_redirects=True,
            )
            if r2.status_code != 200:
                self.unavailable_reason = f"agreement POST HTTP {r2.status_code}"
                return False
            # Senate eFD sometimes serves an Akamai maintenance page (HTML body
            # containing "Site Under Maintenance"). Detect and surface that.
            if "Site Under Maintenance" in r2.text or "under maintenance" in r2.text.lower():
                self.unavailable_reason = "eFD is in maintenance mode"
                return False
            # Refresh CSRF from the post-agreement /search/ page
            new_token = self._extract_csrf(r2.text)
            self.csrf_token = new_token or token
            self.agreed = True
            return True
        except Exception as e:
            self.unavailable_reason = f"agreement flow exception: {e}"
            return False

    # ── public methods ───────────────────────────────────────────────────────

    def search_ptrs(self, start_date: date, end_date: Optional[date] = None,
                    max_records: int = 500) -> List[Dict]:
        """Return rows like {filer, first_name, last_name, full_name, ptr_uuid,
        ptr_url, filed_date (ISO)}."""
        if not self._accept_agreement():
            return []
        results: List[Dict] = []
        page = 0
        length = 100
        end_str = end_date.strftime("%m/%d/%Y 23:59:59") if end_date else ""
        while True:
            try:
                r = self.session.post(
                    f"{SENATE_EFD_BASE}/search/report/data/",
                    data={
                        "csrfmiddlewaretoken":   self.csrf_token,
                        "report_types":          f"[{EFD_REPORT_TYPE_PTR}]",
                        "filer_types":           "[1]",      # 1 = Senator
                        "submitted_start_date":  start_date.strftime("%m/%d/%Y 00:00:00"),
                        "submitted_end_date":    end_str,
                        "candidate_state":       "",
                        "senator_state":         "",
                        "office_id":             "",
                        "first_name":            "",
                        "last_name":             "",
                        "start":                 str(page * length),
                        "length":                str(length),
                    },
                    headers={
                        "Referer": f"{SENATE_EFD_BASE}/search/",
                        "X-CSRFToken": self.csrf_token,
                        "X-Requested-With": "XMLHttpRequest",
                    },
                    timeout=20,
                )
                if r.status_code != 200:
                    self.unavailable_reason = f"search POST HTTP {r.status_code}"
                    break
                data = r.json()
            except Exception as e:
                self.unavailable_reason = f"search exception: {e}"
                break

            rows = data.get("data", [])
            if not rows:
                break

            for row in rows:
                if len(row) < 5:
                    continue
                # eFD DataTables row: [first_name, last_name, full_name, html_link, filed_date_mdy]
                first, last, full, html_link, filed = row[0], row[1], row[2], row[3], row[4]
                m = re.search(r'/search/view/ptr/([^/"]+)/', html_link or "")
                if not m:
                    continue
                uuid = m.group(1)
                # eFD's full_name comes in as "Last, First Middle (Senator)".
                # Normalize to "First Middle Last" so the roster lookup hits.
                cleaned = re.sub(r"\s*\(Senator\)\s*$", "", full or "").strip()
                if "," in cleaned:
                    last_part, first_part = cleaned.split(",", 1)
                    cleaned = f"{first_part.strip()} {last_part.strip()}".strip()
                # Fall back to building from the first_name/last_name cells if
                # the full_name column was malformed.
                if not cleaned:
                    cleaned = f"{first.strip()} {last.strip()}".strip()
                results.append({
                    "first_name":  first.strip(),
                    "last_name":   last.strip(),
                    "full_name":   cleaned,
                    "ptr_uuid":    uuid,
                    "ptr_url":     f"{SENATE_EFD_BASE}/search/view/ptr/{uuid}/",
                    "filed_date":  _iso_from_mdy(filed),
                })
                if len(results) >= max_records:
                    break

            total = int(data.get("recordsTotal") or 0)
            page += 1
            if len(results) >= max_records or page * length >= total or len(rows) < length:
                break

        return results

    def fetch_transactions(self, ptr_uuid: str) -> List[Dict]:
        """Return parsed transactions for one PTR. Cached on disk forever."""
        cache_file = os.path.join(_CACHE_DIR, f"ptr_{ptr_uuid}.json")
        if os.path.exists(cache_file):
            try:
                with open(cache_file) as f:
                    return json.load(f)
            except Exception:
                pass

        if not self._accept_agreement():
            return []
        try:
            r = self.session.get(
                f"{SENATE_EFD_BASE}/search/view/ptr/{ptr_uuid}/",
                headers={"Referer": f"{SENATE_EFD_BASE}/search/"},
                timeout=15,
            )
            if r.status_code != 200:
                return []
            html_text = r.text
        except Exception:
            return []

        parser = _PTRTransactionParser()
        try:
            parser.feed(html_text)
        except Exception:
            return []

        transactions = []
        for row in parser.rows:
            if len(row) < 8:
                continue
            # Columns: # | TxDate | Owner | Ticker | AssetName | AssetType | Type | Amount | Comment(opt)
            tx_date_str = row[1] if len(row) > 1 else ""
            owner       = row[2] if len(row) > 2 else ""
            ticker      = row[3] if len(row) > 3 else ""
            asset_name  = row[4] if len(row) > 4 else ""
            asset_type  = row[5] if len(row) > 5 else ""
            tx_type     = row[6] if len(row) > 6 else ""
            amount      = row[7] if len(row) > 7 else ""
            comment     = row[8] if len(row) > 8 else ""

            # Skip placeholder rows where # column has no real index
            if not row[0].strip().isdigit():
                continue

            ticker_clean = re.sub(r"[^A-Z0-9.]", "", ticker.upper().split()[0] if ticker else "")
            if ticker_clean in ("--", ""):
                ticker_clean = ""

            transactions.append({
                "transaction_date": _iso_from_mdy(tx_date_str),
                "owner":            owner,
                "ticker":           ticker_clean,
                "asset_name":       asset_name,
                "asset_type":       asset_type,
                "type":             tx_type,
                "amount":           amount,
                "comment":          comment,
            })

        try:
            with open(cache_file, "w") as f:
                json.dump(transactions, f)
        except Exception:
            pass
        return transactions


# Module-level singleton (lazy)
_efd_scraper: Optional[SenateEFDScraper] = None


def _get_scraper() -> Optional[SenateEFDScraper]:
    global _efd_scraper
    if _efd_scraper is None and requests is not None:
        _efd_scraper = SenateEFDScraper()
    return _efd_scraper


# ── Dynamic member roster (Congress.gov + senate.gov + KNOWN_MEMBERS) ─────────

_roster_cache: Dict[str, Dict] = {}   # name.lower() → member dict


def _load_roster() -> Dict[str, Dict]:
    """
    Build a name-keyed member lookup. Priority:
      Congress.gov API (if key set) → senate.gov JSON → KNOWN_MEMBERS table.
    Cached for process lifetime.
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
    """Fetch the live 119th Congress roster via Congress.gov API.

    Joins:
      /member?currentMember=true&congress=...&limit=250  (paged)
      /member/{bioguideId}                                (terms + leadership)
      /committee/{c}/{ch}                                 (committee → members)

    Returns enriched member dicts with real committee assignments when the API
    key is set. Without a key this returns [] (caller falls back to senate.gov
    JSON + KNOWN_MEMBERS).
    """
    if not CONGRESS_GOV_API_KEY:
        return []

    members: List[Dict] = []
    bioguide_to_member: Dict[str, Dict] = {}

    # 1) Roster (paginated) ─────────────────────────────────────────────────
    for chamber_name, chamber_param in [("Senate", "Senate"), ("House", "House")]:
        offset = 0
        while True:
            url = (f"{CONGRESS_GOV_BASE}/member"
                   f"?currentMember=true&congress={CURRENT_CONGRESS}&chamber={chamber_param}"
                   f"&format=json&limit=250&offset={offset}"
                   f"&api_key={CONGRESS_GOV_API_KEY}")
            data = _get_json(url, timeout=12)
            if "error" in data:
                break
            page_members = data.get("members", [])
            if not page_members:
                break
            for m in page_members:
                raw_name = m.get("name", "")
                if "," in raw_name:
                    parts = raw_name.split(",", 1)
                    full_name = f"{parts[1].strip()} {parts[0].strip()}"
                else:
                    full_name = raw_name
                party_raw = m.get("partyName", m.get("party", ""))
                party = ("D" if "democrat" in party_raw.lower()
                         else "R" if "republican" in party_raw.lower()
                         else "I")
                state = _state_to_abbrev(m.get("state", ""))
                district = m.get("district", "")
                bioguide = m.get("bioguideId", "")
                member = {
                    "id":          _make_member_id(full_name),
                    "full_name":   full_name,
                    "party":       party,
                    "state":       state,
                    "district":    str(district) if district else "",
                    "chamber":     chamber_name,
                    "committees":  [],
                    "bioguide_id": bioguide,
                    "image_url":   (m.get("depiction") or {}).get("imageUrl", ""),
                }
                if bioguide:
                    bioguide_to_member[bioguide] = member
                members.append(member)
            # Pagination
            next_url = (data.get("pagination") or {}).get("next")
            if not next_url:
                break
            offset += 250
            if offset > 1000:  # sanity
                break

    if not members:
        return []

    # 2) Committee → members join ───────────────────────────────────────────
    # Congress.gov's JSON API does NOT publish member→committee assignments
    # (a known gap). The canonical structured source is the public-domain
    # unitedstates/congress-legislators repo (committee-membership-current.yaml),
    # which is updated continuously and used by ProPublica, GovTrack, etc.
    assignments = _fetch_us_io_committee_assignments()
    for member in members:
        bid = member.get("bioguide_id", "")
        if bid and bid in assignments:
            member["committees"] = [_canon_committee(c) for c in assignments[bid]]

    # 3) Last-resort: fill from KNOWN_MEMBERS where API returned no committees
    for m in members:
        if not m["committees"]:
            known = KNOWN_MEMBERS.get(m["full_name"].lower(), {})
            if known:
                m["committees"] = list(known.get("committees", []))

    return members


def _canon_committee(name: str) -> str:
    """Strip 'Committee on ' prefix etc. to align with COMMITTEE_SECTOR_MAP keys."""
    s = re.sub(r"^Committee on\s+", "", name, flags=re.IGNORECASE).strip()
    # Map a few alternative spellings to keys in COMMITTEE_SECTOR_MAP
    alias = {
        "Banking, Housing and Urban Affairs": "Banking, Housing, and Urban Affairs",
        "Agriculture, Nutrition and Forestry": "Agriculture, Nutrition, and Forestry",
        "Health, Education, Labor and Pensions": "Health, Education, Labor, and Pensions",
        "Commerce, Science and Transportation": "Commerce, Science, and Transportation",
    }
    return alias.get(s, s)


def _fetch_us_io_committee_assignments() -> Dict[str, List[str]]:
    """Return {bioguide_id: [committee_name, ...]} from the public-domain
    unitedstates/congress-legislators repo. 24h disk cache.

    This is the canonical structured source for U.S. congressional committee
    membership — Congress.gov's JSON API doesn't expose it. The repo is
    maintained by a community of volunteers (GovTrack, ProPublica alumni)
    and tracked by https://github.com/unitedstates/congress-legislators.
    """
    cache_file = os.path.join(_CACHE_DIR, "us_io_committee_assignments.json")
    if os.path.exists(cache_file) and (time.time() - os.path.getmtime(cache_file)) < 86400:
        try:
            with open(cache_file) as f:
                return json.load(f)
        except Exception:
            pass

    if requests is None:
        return {}
    try:
        import yaml  # PyYAML — present in our build env
    except ImportError:
        return {}

    BASE = "https://raw.githubusercontent.com/unitedstates/congress-legislators/main"
    try:
        r1 = requests.get(f"{BASE}/committees-current.yaml", timeout=15,
                          headers={"User-Agent": "FinceptTerminal/1.0"})
        r2 = requests.get(f"{BASE}/committee-membership-current.yaml", timeout=15,
                          headers={"User-Agent": "FinceptTerminal/1.0"})
        if r1.status_code != 200 or r2.status_code != 200:
            return {}
        committees_doc = yaml.safe_load(r1.text) or []
        membership_doc = yaml.safe_load(r2.text) or {}
    except Exception:
        return {}

    # thomas_id → short committee name (strip "{Senate,House,Joint} Committee on")
    thomas_to_name: Dict[str, str] = {}
    for c in committees_doc:
        tid = c.get("thomas_id", "")
        if not tid:
            continue
        nm = c.get("name", "")
        nm = re.sub(r"^(Senate|House|Joint)\s+Committee on\s+", "", nm)
        nm = re.sub(r"^(Senate|House|Joint)\s+Permanent\s+Select\s+Committee on\s+",
                    "", nm, flags=re.IGNORECASE)
        nm = re.sub(r"^(Senate|House|Joint)\s+Select\s+Committee on\s+",
                    "", nm, flags=re.IGNORECASE)
        nm = re.sub(r"^Committee on\s+", "", nm)
        nm = re.sub(r"^the\s+", "", nm, flags=re.IGNORECASE)
        thomas_to_name[tid] = nm

    # Walk membership: top-level key is thomas_id (e.g. "SSAF"); subcommittees
    # are "SSAF01" etc. Roll up to the parent committee name.
    out: Dict[str, List[str]] = {}
    for tid, members_list in membership_doc.items():
        parent_tid = tid[:4]
        cmte_name = thomas_to_name.get(parent_tid)
        if not cmte_name:
            continue
        for m in (members_list or []):
            bid = m.get("bioguide", "")
            if not bid:
                continue
            existing = out.setdefault(bid, [])
            if cmte_name not in existing:
                existing.append(cmte_name)

    try:
        with open(cache_file, "w") as f:
            json.dump(out, f)
    except Exception:
        pass
    return out


def _fetch_senate_gov_members() -> List[Dict]:
    """Public fallback senator roster from senate.gov — no API key needed."""
    data = _get_json(SENATE_CONTACT_JSON, timeout=8)
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
    parts = key.split()
    if len(parts) >= 2:
        last = parts[-1]
        for k, v in roster.items():
            if k.split()[-1] == last:
                return v
    default = "Senate" if "senate" in chamber_hint.lower() else "House"
    return {"party": "", "state": "", "chamber": default, "committees": []}


# ── Senate PTR fetching (via SenateEFDScraper) ────────────────────────────────

def fetch_senate_ptrs(days_back: int = 90, max_records: int = 300) -> List[Dict]:
    """
    Fetch Senate PTR filings + parsed transactions via efdsearch.senate.gov.
    Returns one filing dict per PTR with a 'trades' list.

    Transaction-page fetches run in parallel (eFD has no batch endpoint and
    each PTR is on its own URL). The session's CSRF/agreement state is set
    once by search_ptrs() before we get here; workers then only issue read-only
    GETs against the already-authenticated session. The per-filing detail
    pages don't return Set-Cookie, so session-state mutation under concurrency
    is not exercised here — only the connection pool is, which requests's
    urllib3 backend is designed for.
    """
    scraper = _get_scraper()
    if scraper is None:
        return []

    start = date.today() - timedelta(days=days_back)
    rows  = scraper.search_ptrs(start, max_records=max_records)
    if not rows:
        return []

    # Parallel transaction fetch. Cache hits return immediately; only first-time
    # UUIDs hit the network. ThreadPoolExecutor preserves submission order via
    # map(), so the zip below stays aligned.
    uuids = [r.get("ptr_uuid", "") for r in rows]
    workers = min(_EFD_PARALLEL_WORKERS, max(1, len(uuids)))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        txns_per_row = list(pool.map(
            lambda u: scraper.fetch_transactions(u) if u else [],
            uuids,
        ))

    skip_types = {"trust", "retirement", "529", "ira", "savings bond",
                  "pension", "annuity", "checking", "money market"}

    filings: List[Dict] = []
    for row, txns in zip(rows, txns_per_row):
        ptr_uuid    = row.get("ptr_uuid", "")
        member_name = row.get("full_name", "").strip() or "Unknown"
        filed_date  = row.get("filed_date", "")
        filing_url  = row.get("ptr_url", "")
        if not ptr_uuid or not txns:
            # No UUID, or paper-filed PTR with no machine-readable transactions.
            continue

        enrich     = _enrich_member(member_name, "senate")
        committees = enrich.get("committees", [])

        trades = []
        for t in txns:
            asset_type = t.get("asset_type", "")
            if any(s in (asset_type or "").lower() for s in skip_types):
                continue
            lo, hi    = _parse_amount_range(t.get("amount", ""))
            direction = _map_direction(t.get("type", ""))
            at_mapped = _map_asset_type(asset_type)
            ticker    = t.get("ticker", "")
            cmte_rel  = _compute_committee_relevance(ticker, committees) if ticker else ""
            trades.append({
                "ticker":              ticker,
                "asset_name":          t.get("asset_name", ""),
                "asset_type":          at_mapped,
                "direction":           direction,
                "transaction_date":    t.get("transaction_date", ""),
                "amount_low":          lo,
                "amount_high":         hi,
                "amount_range_label":  t.get("amount", ""),
                "committee_relevance": cmte_rel,
            })

        if not trades:
            continue

        first_tx = trades[0].get("transaction_date", "")
        lag      = _compute_lag(first_tx, filed_date) if first_tx and filed_date else 0

        filings.append({
            "member_name":         member_name,
            "chamber":             "senate",
            "party":               enrich.get("party", ""),
            "state":               enrich.get("state", ""),
            "committees":          committees,
            "filed_date":          filed_date,
            "disclosure_lag_days": lag,
            "trades":              trades,
            "filing_url":          filing_url,
            "data_source":         "Senate eFD",
        })

    return filings


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
    """Parse the House FD master-index ZIP.

    The ZIP at {year}FD.zip contains {year}FD.xml — a master index listing one
    <Member> element per filing.  Individual transaction details are in separate
    PDF files that are not bundled in the ZIP, so we build one synthetic trade
    entry per PTR filing (the filing itself is the signal — member, date, DocID).
    """
    cache_dir  = os.path.join(_CACHE_DIR, "house_fd")
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
            resp = requests.get(zip_url, timeout=15,
                                headers={"User-Agent": "Mozilla/5.0 (compatible; FinceptTerminal/1.0)"})
            if resp.status_code != 200:
                return []
            zip_data = resp.content
            with open(cache_file, "wb") as f:
                f.write(zip_data)

        filings = []
        with zipfile.ZipFile(io.BytesIO(zip_data)) as zf:
            index_name = f"{year}FD.xml"
            if index_name not in zf.namelist():
                index_name = next((n for n in zf.namelist() if n.lower() == f"{year}fd.xml"), None)
            if not index_name:
                print(f"[house_fds] WARNING: {year}FD.xml not found in ZIP. Contents: {zf.namelist()}", flush=True)
                return []
            filings = _parse_house_index_xml(zf.read(index_name), cutoff, year)
        return filings
    except Exception:
        return []


def _parse_house_index_xml(xml_bytes: bytes, cutoff: date, year: int) -> list:
    """Parse the House FD master index XML.

    Each <Member> element has: Prefix, Last, First, Suffix, FilingType,
    StateDst, Year, FilingDate, DocID.
    FilingType "P" = Periodic Transaction Report (PTR) — what we want.
    """
    try:
        root = ET.fromstring(xml_bytes)
    except Exception:
        return []

    filings = []
    members_el = root if root.tag in ("Members", "MemberIndex") else root.find("Members") or root.find("MemberIndex")
    if members_el is None:
        members_el = root

    for member_el in members_el:
        if member_el.tag != "Member":
            continue
        try:
            filing_type = (member_el.findtext("FilingType") or "").strip().upper()
            if filing_type not in ("P", "PTR"):
                continue

            first     = (member_el.findtext("First")    or "").strip()
            last      = (member_el.findtext("Last")     or "").strip()
            state_dst = (member_el.findtext("StateDst") or "").strip()
            state     = state_dst[:2] if state_dst else ""
            filed_raw = (member_el.findtext("FilingDate") or "").strip()
            doc_id    = (member_el.findtext("DocID")    or "").strip()

            try:
                filed_date = datetime.strptime(filed_raw, "%m/%d/%Y").date()
            except Exception:
                continue

            if filed_date < cutoff:
                continue

            member_name = f"{first} {last}".strip()
            if not member_name:
                continue

            enrich     = _enrich_member(member_name, "house")
            party      = enrich.get("party", "")
            committees = enrich.get("committees", [])
            doc_url    = (f"{HOUSE_FDS_BASE}/public_disc/ptr-pdfs/{year}/{doc_id}.pdf"
                          if doc_id else "")

            trades = [{
                "ticker":              "",
                "asset_name":          "PTR Filing (see disclosure PDF)",
                "asset_type":          "equity",
                "direction":           "unknown",
                "transaction_date":    filed_date.isoformat(),
                "amount_low":          0.0,
                "amount_high":         0.0,
                "amount_range_label":  "",
                "committee_relevance": "",
                "placeholder":         True,
            }]

            filings.append({
                "member_name":         member_name,
                "chamber":             "house",
                "party":               party,
                "state":               state,
                "committees":          committees,
                "filed_date":          filed_date.isoformat(),
                "disclosure_lag_days": None,
                "trades":              trades,
                "filing_url":          doc_url,
                "data_source":         "House FDS",
            })
        except Exception:
            continue

    return filings


# ── Signal scoring ────────────────────────────────────────────────────────────

def compute_signal_score(trade: dict, member: dict) -> float:
    score       = 0.0
    ticker      = trade.get("ticker", "")
    committees  = member.get("committees", [])
    amount_high = trade.get("amount_high", 0)
    cmte_rel    = trade.get("committee_relevance", "")

    if cmte_rel:
        score += 30
    else:
        cmte_for_ticker = TICKER_COMMITTEE_MAP.get(ticker.upper(), "")
        if cmte_for_ticker and any(cmte_for_ticker[:8].lower() in c.lower() for c in committees):
            score += 20

    lag = trade.get("disclosure_lag_days", 0)
    if lag > 40:    score += 15
    elif lag > 30:  score += 8
    elif lag > 20:  score += 3

    if amount_high >= 1_000_000:   score += 10
    elif amount_high >= 250_000:   score += 5
    elif amount_high >= 50_000:    score += 2

    return min(100.0, score)


# ── Full summary builder ───────────────────────────────────────────────────────

def _senate_reachable() -> bool:
    """Probe efdsearch.senate.gov. 200 home, 302→home, or even 503 maintenance
    all count as 'reachable' so the scraper can try and report its own error."""
    if requests is None:
        return False
    try:
        r = requests.head(f"{SENATE_EFD_BASE}/search/home/", timeout=4,
                          allow_redirects=False)
        return r.status_code in (200, 301, 302, 303, 307, 308)
    except Exception:
        return False


def _house_reachable() -> bool:
    if requests is None:
        return False
    try:
        r = requests.head(HOUSE_FDS_BASE, timeout=4, allow_redirects=False)
        return r.status_code in (200, 301, 302, 303, 307, 308)
    except Exception:
        return False


def build_all_data(days_back: int = 90) -> dict:
    """
    Build {members, trades} compatible with PowerTraderService.parse_summary().
    Probes each source independently — they're on different domains.
    """
    senate_online = _senate_reachable()
    house_online  = _house_reachable()

    senate_filings: list = []
    house_filings:  list = []

    if senate_online or house_online:
        _load_roster()

    if senate_online:
        senate_filings = fetch_senate_ptrs(days_back=days_back)

    if house_online:
        house_filings = fetch_house_ptrs(days_back=days_back)

    all_filings = senate_filings + house_filings

    if not all_filings:
        scraper = _get_scraper()
        return {
            "members": [],
            "trades": [],
            "diagnostics": {
                "senate_online":   senate_online,
                "house_online":    house_online,
                "senate_filings":  len(senate_filings),
                "house_filings":   len(house_filings),
                "scraper_reason":  scraper.unavailable_reason if scraper else "no scraper",
                "has_congress_gov_key": bool(CONGRESS_GOV_API_KEY),
            },
        }

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
        lag         = filing.get("disclosure_lag_days")

        if member_id not in member_map:
            # Pull additional fields (district, image) from roster when available
            roster_entry = _enrich_member(member_name, chamber_raw)
            member_map[member_id] = {
                "id":                   member_id,
                "full_name":            member_name,
                "party":                party or roster_entry.get("party", ""),
                "chamber":              chamber,
                "state":                state or roster_entry.get("state", ""),
                "district":             roster_entry.get("district", ""),
                "committees":           committees or roster_entry.get("committees", []),
                "term_start":           "",
                "estimated_net_worth":  0.0,
                "trade_count_ytd":      0,
                "portfolio_return_ytd": 0.0,
                "spy_return_ytd":       0.0,
                "bioguide_id":          roster_entry.get("bioguide_id", ""),
                "image_url":            roster_entry.get("image_url", ""),
            }

        mem = member_map[member_id]
        if not mem["party"] and party:
            mem["party"] = party
        if not mem["committees"] and committees:
            mem["committees"] = committees

        for t in filing.get("trades", []):
            trade_idx += 1
            mem["trade_count_ytd"] += 1
            is_placeholder = t.get("placeholder", False)
            score = 0.0 if is_placeholder else compute_signal_score(
                {**t, "disclosure_lag_days": lag or 0},
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
                "data_source":         filing.get("data_source", "Senate eFD"),
                "placeholder":         is_placeholder,
            })

    trades.sort(key=lambda x: x.get("disclosure_date", ""), reverse=True)
    return {
        "members": list(member_map.values()),
        "trades":  trades,
        "diagnostics": {
            "senate_filings":  len(senate_filings),
            "house_filings":   len(house_filings),
            "has_congress_gov_key": bool(CONGRESS_GOV_API_KEY),
        },
    }


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
