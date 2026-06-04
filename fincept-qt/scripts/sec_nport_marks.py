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

# SEC asks for ≤10 req/s shared across User-Agent. PreIpoService runs up to 3
# SEC scripts concurrently, so per-process throttle must be ≥0.34s to stay
# under the shared ceiling. A tiny safety margin makes it 0.4s.
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


def _cik_unpadded(cik):
    return str(int(str(cik).lstrip("0") or "0"))


# ── Fund universe ─────────────────────────────────────────────────────────────
# One entry per registrant CIK. Multiple fund series usually share a CIK at
# the trust level (e.g. all Fidelity Contrafund + Blue Chip + Growth Co. file
# under 0000024238 — "Fidelity Concord Street Trust" / similar wrappers), so
# we de-duplicate by CIK to avoid hitting the same N-PORT-P twice and to
# avoid mis-attributing the marks to whichever fund's label happens to come
# first in iteration order. Labels are intentionally generic.

FUND_FAMILIES = [
    ("Fidelity (Concord/Securities Trust)", "0000024238"),
    ("T. Rowe Price funds",                 "0000080241"),
    ("BlackRock Capital Appreciation",       "0000051931"),
    ("Hartford / Wellington",                "0000048771"),
    ("Franklin DynaTech",                    "0000038723"),
    ("Baron funds",                          "0000810902"),
    ("Vanguard PRIMECAP",                    "0000036405"),
    ("Morgan Stanley Inst Growth",           "0000741375"),
    ("JPMorgan Large Cap Growth",            "0000763852"),
]

# Known private-company aliases — effectively a **default watchlist** of
# well-known late-stage private companies that mutual funds disclose marks
# for. This list exists for two reasons:
#
#  1. N-PORT just flags `<isRestrictedSec>Y</isRestrictedSec>` — it doesn't
#     say "this is a famous unicorn vs an employee SPV". Most restricted
#     holdings are nonsense to a retail user (regional subsidiaries, GP
#     side vehicles, employee equity vehicles). Without an explicit set of
#     names to surface, the dossier would be flooded with junk.
#
#  2. Issuer strings vary widely between funds for the same company
#     ("Stripe, Inc." vs "STRIPE INC CL B" vs "Stripe Series H Pref"), so
#     an explicit alias-set + word-boundary regex is what merges them into
#     a single CompanyDossier in the C++ layer.
#
# This should become user-editable (Settings → "Pre-IPO Watchlist", JSON in
# FINCEPT_DATA_DIR). Until then this hardcoded set IS the universe.
PRIVATE_NAMES = {
    # cid:         { aliases (substring-anchored to word boundary), known_cik }
    "stripe":      {"aliases": ["Stripe"],                    "cik": "0001646180"},
    "spacex":      {"aliases": ["Space Exploration", "SpaceX"], "cik": "0001181412"},
    "openai":      {"aliases": ["OpenAI"],                    "cik": ""},
    "anthropic":   {"aliases": ["Anthropic"],                 "cik": ""},
    "databricks":  {"aliases": ["Databricks"],                "cik": ""},
    "bytedance":   {"aliases": ["ByteDance"],                 "cik": ""},
    "canva":       {"aliases": ["Canva"],                     "cik": ""},
    "klarna":      {"aliases": ["Klarna"],                    "cik": ""},
    "discord":     {"aliases": ["Discord"],                   "cik": ""},
    "epic-games":  {"aliases": ["Epic Games"],                "cik": ""},
    "miro":        {"aliases": ["Miro Inc", "RealtimeBoard"], "cik": ""},
    "checkr":      {"aliases": ["Checkr"],                    "cik": ""},
    "nuro":        {"aliases": ["Nuro"],                      "cik": ""},
    "instacart":   {"aliases": ["Maplebear", "Instacart"],    "cik": ""},
    "reddit":      {"aliases": ["Reddit"],                    "cik": ""},
    "scale-ai":    {"aliases": ["Scale AI"],                  "cik": ""},
    "xai":         {"aliases": ["xAI", "X.AI"],               "cik": ""},
    "perplexity":  {"aliases": ["Perplexity"],                "cik": ""},
    "figma":       {"aliases": ["Figma"],                     "cik": ""},
    "ramp":        {"aliases": ["Ramp Business"],             "cik": ""},
    "brex":        {"aliases": ["Brex Inc"],                  "cik": ""},
    "rippling":    {"aliases": ["Rippling"],                  "cik": ""},
    "notion":      {"aliases": ["Notion Labs"],               "cik": ""},
    "vanta":       {"aliases": ["Vanta Inc"],                 "cik": ""},
    "wiz":         {"aliases": ["Wiz Inc", "Wiz Holdings"],   "cik": ""},
    "shein":       {"aliases": ["SHEIN", "Roadget Business"], "cik": ""},
    "neuralink":   {"aliases": ["Neuralink"],                 "cik": ""},
    "fanatics":    {"aliases": ["Fanatics"],                  "cik": ""},
    "kraken":      {"aliases": ["Payward", "Kraken"],         "cik": ""},
    "chime":       {"aliases": ["Chime Financial"],           "cik": ""},
    "plaid":       {"aliases": ["Plaid Inc"],                 "cik": ""},
    "anduril":     {"aliases": ["Anduril"],                   "cik": ""},
}

# Pre-compile per-alias word-boundary regex once. The previous substring
# match treated "RampUp Inc" as Ramp, "Stripeworth" as Stripe, etc.
_ALIAS_RES = []
for _cid, _e in PRIVATE_NAMES.items():
    for _a in _e["aliases"]:
        _ALIAS_RES.append((_cid, _e["aliases"][0], _e.get("cik", ""),
                           re.compile(r"\b" + re.escape(_a) + r"\b", re.IGNORECASE)))


def _match_private_id(issuer_name):
    """Return (cid, canonical_name, cik) or (None, None, None)."""
    if not issuer_name:
        return None, None, None
    for cid, canonical, cik, rx in _ALIAS_RES:
        if rx.search(issuer_name):
            return cid, canonical, cik
    return None, None, None


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
        cid, canonical, alias_cik = _match_private_id(issuer)
        if cid:
            marks.append({
                "issuer_raw": issuer,
                "company_id": cid,
                "company_name": canonical,
                "company_cik": alias_cik,
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
                    "company_cik": m.get("company_cik", ""),
                }
                marks_by_company.setdefault(cid, []).append(entry)
                fund_holdings.setdefault(cik, []).append(entry)
    # Sort each company's marks by as_of desc
    for cid, lst in marks_by_company.items():
        lst.sort(key=lambda x: x.get("as_of", ""), reverse=True)
    # Emit a separate name+CIK map so the C++ side can join Form D and N-PORT
    # universes without relying on slugified-name collisions.
    aliases = {
        cid: {
            "name": e["aliases"][0],
            "cik":  e.get("cik", ""),
            "aliases": list(e["aliases"]),
        }
        for cid, e in PRIVATE_NAMES.items()
    }
    return {
        "marks": marks_by_company,
        "fund_holdings": fund_holdings,
        "fund_families": [{"label": l, "cik": c} for l, c in families],
        "aliases": aliases,
    }


def family_cursor(families_max=None):
    """Newest N-PORT-P accession per fund family — the data-based change signal
    for marks. Accessions are stable identifiers (no sliding-window churn), and
    each costs one cheap submissions request, so this is far cheaper than the
    per-filing XML the full collect_all_marks fetches. Marks only change when a
    family files a new N-PORT (quarterly), so an unchanged map = nothing to do."""
    families = FUND_FAMILIES if families_max is None else FUND_FAMILIES[:families_max]
    cur = {}
    for _label, cik in families:
        accs = list_nport_accessions(cik, limit=1)
        cur[cik] = accs[0]["adsh"] if accs else ""
    return cur


def handle_action(action, payload):
    if action == "marks_all":
        families_max = payload.get("families_max")
        # Cheap cursor probe first: if every family's newest N-PORT accession
        # matches the prior cursor, nothing was filed → skip all the XML.
        cursor = family_cursor(families_max)
        prev = payload.get("prev_cursor")
        # any(cursor.values()): never trust a degenerate all-empty probe (every
        # family returned "" — an EDGAR outage or a fully-failed run) as
        # "unchanged"; that would equal a prior failed cursor and suppress the
        # fetch forever. Require at least one real accession before short-circuiting.
        if (isinstance(prev, dict) and prev.get("accessions") == cursor
                and any(cursor.values())):
            return {"unchanged": True, "cursor": {"accessions": cursor}}
        data = collect_all_marks(
            quarters_back=payload.get("quarters_back", 2),
            families_max=families_max,
        )
        data["cursor"] = {"accessions": cursor}
        return data
    if action == "marks_for":
        name = (payload.get("name") or "").strip()
        if not name:
            return []
        all_data = collect_all_marks(quarters_back=payload.get("quarters_back", 2))
        # Direct cid hit first (e.g. payload {"name": "stripe"})
        if name in all_data["marks"]:
            return all_data["marks"][name]
        # Otherwise match against the alias regex set: does any cid have an
        # alias matching the input?
        cid, _canonical, _cik = _match_private_id(name)
        return all_data["marks"].get(cid, []) if cid else []
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
