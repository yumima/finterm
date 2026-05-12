"""
SEC EDGAR N-PORT-P mutual fund marks extractor.

Mutual funds that hold late-stage private equity (Stripe, SpaceX, Anthropic,
ByteDance, OpenAI, Databricks, etc.) disclose their fair-value marks every
quarter in N-PORT-P filings. Multiple funds' marks for the same name = a real
market signal — Tape D and Morningstar's private-co coverage are built on
exactly this stream.

This script:
  1. Lists known fund-family CIKs (Fidelity, T. Rowe Price, BlackRock, Wellington,
     Franklin, Baron, Vanguard, Morgan Stanley, JPMorgan Growth).
  2. For each, hits data.sec.gov/submissions/CIK*.json to find recent N-PORT-P
     accession numbers.
  3. Fetches primary_doc.xml; pulls every `<invstOrSec>` with `<isRestrictedSec>Y</isRestrictedSec>`
     and an issuer name we can recognise as a known private company.
  4. Emits {company_name -> [marks]} keyed for fuzzy match into the universe.

Actions:
  marks_all        — {marks: {company_name: [{fund, fund_cik, as_of, fair_value, shares, pps}, ...]}}
  marks_for        — payload: {"name": "Stripe"} → list of marks for a single issuer
  fund_holdings    — payload: {"fund_cik": "..."} → all restricted holdings for a fund

Free, public, no API key. Polite rate-limited User-Agent.
"""

import json
import re
import sys
import time
from xml.etree import ElementTree as ET

try:
    import requests
except ImportError:
    requests = None

SUBMISSIONS = "https://data.sec.gov/submissions"
ARCHIVE = "https://www.sec.gov/Archives/edgar/data"
UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
}

_LAST_REQ = 0.0


def _get(url, params=None, headers=None, timeout=15):
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < 0.12:
        time.sleep(0.12 - elapsed)
    try:
        r = requests.get(url, params=params, timeout=timeout, headers=headers or UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        return None


def _cik_padded(cik):
    return str(cik).zfill(10)


def _cik_unpadded(cik):
    return str(int(str(cik).lstrip("0") or "0"))


# ── Fund universe ─────────────────────────────────────────────────────────────
# Each entry: (display_name, CIK of the fund/series). Fund families file at
# multiple CIK levels (registrant, series, class). The registrant-level CIK is
# enough to find N-PORT-P submissions because EDGAR rolls them up.

FUND_FAMILIES = [
    # Fidelity — Contrafund, Blue Chip Growth, Growth Company, OTC
    ("Fidelity Contrafund (FCNTX)",          "0000024238"),
    ("Fidelity Blue Chip Growth (FBGRX)",    "0000024238"),
    ("Fidelity Growth Company (FDGRX)",      "0000024238"),
    ("Fidelity OTC Portfolio (FOCPX)",       "0000024238"),
    # T. Rowe Price — Growth Stock, New Horizons, Blue Chip Growth
    ("T. Rowe Price Growth Stock (PRGFX)",   "0000080241"),
    ("T. Rowe Price New Horizons (PRNHX)",   "0000080241"),
    ("T. Rowe Price Blue Chip Growth (TRBCX)", "0000080241"),
    # BlackRock
    ("BlackRock Capital Appreciation",        "0000051931"),
    # Wellington / Hartford
    ("Hartford Growth Opportunities",         "0000048771"),
    # Franklin Templeton
    ("Franklin DynaTech",                     "0000038723"),
    # Baron
    ("Baron Partners (BPTRX)",                "0000810902"),
    ("Baron Focused Growth",                  "0000810902"),
    # Vanguard
    ("Vanguard PRIMECAP",                     "0000036405"),
    # Morgan Stanley Insight
    ("Morgan Stanley Inst Growth",            "0000741375"),
    # JPMorgan
    ("JPMorgan Large Cap Growth",             "0000763852"),
]

# Known private-company aliases. Issuer-name strings vary widely in N-PORT
# (e.g. "Stripe, Inc.", "STRIPE INC CL B", "Stripe Inc - Series H"). We
# normalize to a canonical id by token matching.
PRIVATE_NAMES = {
    "stripe": ["Stripe"],
    "spacex": ["Space Exploration", "SpaceX"],
    "openai": ["OpenAI"],
    "anthropic": ["Anthropic"],
    "databricks": ["Databricks"],
    "bytedance": ["ByteDance", "TikTok"],
    "canva": ["Canva"],
    "klarna": ["Klarna"],
    "discord": ["Discord"],
    "epic-games": ["Epic Games"],
    "miro": ["Miro", "RealtimeBoard"],
    "checkr": ["Checkr"],
    "nuro": ["Nuro"],
    "instacart": ["Maplebear", "Instacart"],
    "reddit": ["Reddit"],
    "scale-ai": ["Scale AI", "Scale Technology"],
    "xai": ["xAI", "X.AI"],
    "perplexity": ["Perplexity"],
    "figma": ["Figma"],
    "ramp": ["Ramp Business"],
    "brex": ["Brex Inc"],
    "rippling": ["Rippling"],
    "notion": ["Notion Labs"],
    "vanta": ["Vanta"],
    "wiz": ["Wiz Inc", "Wiz Holdings"],
    "shein": ["SHEIN", "Roadget Business"],
    "neuralink": ["Neuralink"],
    "fanatics": ["Fanatics"],
    "kraken": ["Payward", "Kraken"],
    "chime": ["Chime Financial"],
    "plaid": ["Plaid Inc"],
}


def _match_private_id(issuer_name):
    if not issuer_name:
        return None, None
    n = issuer_name.upper()
    for cid, aliases in PRIVATE_NAMES.items():
        for a in aliases:
            if a.upper() in n:
                return cid, aliases[0]
    return None, None


# ── N-PORT submissions index ─────────────────────────────────────────────────

def list_nport_accessions(fund_cik, limit=4):
    """Recent N-PORT-P accession numbers for a fund (newest first)."""
    cik_pad = _cik_padded(fund_cik)
    url = f"{SUBMISSIONS}/CIK{cik_pad}.json"
    r = _get(url, headers={**UA, "Host": "data.sec.gov"})
    if r is None:
        return []
    try:
        data = r.json()
    except Exception:
        return []
    recent = data.get("filings", {}).get("recent", {})
    forms = recent.get("form", [])
    adshes = recent.get("accessionNumber", [])
    dates = recent.get("filingDate", [])
    out = []
    for i, f in enumerate(forms):
        if f in ("NPORT-P", "NPORT-P/A"):
            out.append({
                "adsh": adshes[i],
                "filed_date": dates[i] if i < len(dates) else "",
            })
            if len(out) >= limit:
                break
    return out


_NS_RE = re.compile(r"^\{[^}]+\}")


def _strip_ns(tree):
    for el in tree.iter():
        el.tag = _NS_RE.sub("", el.tag)


def fetch_nport_xml(fund_cik, adsh):
    cik_u = _cik_unpadded(fund_cik)
    adsh_clean = adsh.replace("-", "")
    url = f"{ARCHIVE}/{cik_u}/{adsh_clean}/primary_doc.xml"
    r = _get(url, headers={**UA, "Host": "www.sec.gov"})
    if r is None:
        return None
    try:
        root = ET.fromstring(r.content)
    except ET.ParseError:
        return None
    _strip_ns(root)

    as_of = (root.findtext(".//repPdEnd", default="") or
             root.findtext(".//genInfo/repPdEnd", default="") or "").strip()

    marks = []
    for inv in root.findall(".//invstOrSec"):
        restricted = (inv.findtext("isRestrictedSec", default="N") or "").strip().upper()
        if restricted != "Y":
            continue
        issuer = (inv.findtext("name", default="") or "").strip()
        if not issuer:
            continue
        try:
            shares = float(inv.findtext("balance", default="0") or 0)
        except (TypeError, ValueError):
            shares = 0.0
        try:
            fair_value = float(inv.findtext("valUSD", default="0") or 0)
        except (TypeError, ValueError):
            fair_value = 0.0
        pps = (fair_value / shares) if shares > 0 else 0.0
        cid, canonical = _match_private_id(issuer)
        if cid:
            marks.append({
                "issuer_raw": issuer,
                "company_id": cid,
                "company_name": canonical,
                "shares_held": shares,
                "fair_value_usd": fair_value,
                "mark_pps": pps,
                "as_of": as_of,
            })
    return marks


# ── Public entry points ───────────────────────────────────────────────────────

def collect_all_marks(quarters_back=2, families_max=None):
    """For every fund in FUND_FAMILIES, pull the latest N-PORTs and aggregate marks
       keyed by company id. `quarters_back` controls filings per fund."""
    marks_by_company = {}
    fund_holdings = {}  # fund_cik -> [holdings]
    seen_adsh = set()
    families = FUND_FAMILIES if families_max is None else FUND_FAMILIES[:families_max]
    for label, cik in families:
        accessions = list_nport_accessions(cik, limit=quarters_back)
        for a in accessions:
            key = (cik, a["adsh"])
            if key in seen_adsh:
                continue
            seen_adsh.add(key)
            marks = fetch_nport_xml(cik, a["adsh"]) or []
            for m in marks:
                cid = m["company_id"]
                entry = {
                    "fund_name": label,
                    "fund_cik": cik,
                    "filed_date": a["filed_date"],
                    "as_of": m["as_of"] or a["filed_date"],
                    "shares_held": m["shares_held"],
                    "fair_value_usd": m["fair_value_usd"],
                    "mark_pps": m["mark_pps"],
                    "issuer_raw": m["issuer_raw"],
                    "company_name": m["company_name"],
                }
                marks_by_company.setdefault(cid, []).append(entry)
                fund_holdings.setdefault(cik, []).append(entry)
    # Sort each company's marks by as_of desc
    for cid, lst in marks_by_company.items():
        lst.sort(key=lambda x: x.get("as_of", ""), reverse=True)
    return {
        "marks": marks_by_company,
        "fund_holdings": fund_holdings,
        "fund_families": [{"label": l, "cik": c} for l, c in families],
    }


def handle_action(action, payload):
    if action == "marks_all":
        return collect_all_marks(
            quarters_back=payload.get("quarters_back", 2),
            families_max=payload.get("families_max"),
        )
    if action == "marks_for":
        name = (payload.get("name") or "").lower()
        all_data = collect_all_marks(quarters_back=payload.get("quarters_back", 2))
        for cid, marks in all_data["marks"].items():
            if cid == name or any(a.lower() in name for a in PRIVATE_NAMES.get(cid, [])):
                return marks
        return []
    if action == "fund_holdings":
        cik = payload.get("fund_cik")
        if not cik:
            return {"error": "fund_cik required"}
        # Find the family label, then pull just that fund's marks.
        label = next((l for l, c in FUND_FAMILIES if c == cik), cik)
        accessions = list_nport_accessions(cik, limit=payload.get("quarters_back", 2))
        out = []
        for a in accessions:
            for m in fetch_nport_xml(cik, a["adsh"]) or []:
                out.append({
                    "fund_name": label,
                    "fund_cik": cik,
                    "filed_date": a["filed_date"],
                    **m,
                })
        return out
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "marks_all"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(action, payload), indent=2, default=str))
