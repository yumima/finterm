"""
SEC EDGAR Form D fetcher — REAL XML parser.

Pulls Form D filings (Regulation D private placement notices) and parses the
structured primary_doc.xml for each filing. EDGAR full-text search alone returns
only filing metadata; the actual issuer details (industry, address, amount
raised, exemption type) live in the per-filing XML.

Pipeline:
  1. efts.sec.gov/LATEST/search-index — list recent Form D filings (cik + adsh).
  2. https://www.sec.gov/Archives/edgar/data/{cik}/{adsh}/primary_doc.xml — fetch
     structured offering data per filing.
  3. Group by CIK to build a company universe with rounds = list of filings.

Actions (PythonRunner):
  all_data      — combined {form_d_companies, recent_form_d, s1_filings}
                  for PreIpoService::parse_sec_summary().
  form_d_recent — flat list of recent filings only.
  company_rounds — primary rounds for a single CIK (payload: {"cik": "0001234567"}).

All endpoints are public, no API key. EDGAR requires a descriptive User-Agent.
"""

import json
import re
import sys
import time
from datetime import date, datetime, timedelta, timezone
from xml.etree import ElementTree as ET

try:
    import requests
except ImportError:
    requests = None

EDGAR_SEARCH = "https://efts.sec.gov/LATEST/search-index"
EDGAR_SUBMISSIONS = "https://data.sec.gov/submissions"
EDGAR_ARCHIVE = "https://www.sec.gov/Archives/edgar/data"

# Curated CIKs of well-known late-stage private companies that file Form D.
# These are the names pre-IPO investors actually care about. We always
# fetch their filings even if they don't show up in the noisy recent feed.
# Shared with sec_s1_pipeline.py via sec_known_ciks so the Form D universe and
# the S-1 pipeline never drift. (scripts/ is on PYTHONPATH at runtime.)
try:
    from sec_known_ciks import KNOWN_PRIVATE_CIKS
except ImportError:
    # Standalone fallback if the sibling module isn't importable.
    KNOWN_PRIVATE_CIKS = ["0001181412", "0001646180"]

UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
    "Host": "efts.sec.gov",
}
UA_ARCHIVE = dict(UA)
UA_ARCHIVE["Host"] = "www.sec.gov"

# SEC asks for ≤10 req/s shared across User-Agent. PreIpoService runs up to 3
# SEC scripts concurrently, so per-process gap must be ≥0.34s to stay under
# the shared ceiling. Use 0.4s for safety margin.
_LAST_REQ = 0.0
_MIN_REQ_GAP = 0.4


def _get(url, params=None, headers=None, timeout=15):
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < _MIN_REQ_GAP:
        time.sleep(_MIN_REQ_GAP - elapsed)
    try:
        r = requests.get(url, params=params, timeout=timeout,
                         headers=headers or UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        return None


def _cik_padded(cik):
    return str(cik).zfill(10)


def _cik_unpadded(cik):
    return str(int(str(cik).lstrip("0") or "0"))


def _accession_dashed(adsh):
    s = adsh.replace("-", "")
    if len(s) >= 18:
        return f"{s[0:10]}-{s[10:12]}-{s[12:18]}"
    return adsh


def _slug(name):
    return re.sub(r"[^a-z0-9]+", "-", (name or "").lower()).strip("-")


# ── SPV (secondary-interest) resolution ────────────────────────────────────────
# Late-stage private companies don't trade, but the SPVs that raise capital to
# buy into them DO file Form D — and the entity is named after its target
# ("HII Anthropic-01", "Hiive ScaleAI Series I", "Destiny SpaceX SPV LLC").
# Those filings are a free public proxy for secondary-market interest, the
# closest an independent app gets to the proprietary order-flow that
# Hiive/Forge/NPM sell. We resolve the filer name to (target, sponsor).

# Sponsors that package private-company SPVs. "HII" is Hiive's filing prefix.
_SPV_SPONSORS = [
    ("hiive", "Hiive"), ("hii", "Hiive"), ("forge", "Forge"),
    ("equityzen", "EquityZen"), ("destiny", "Destiny"),
    ("manhattan venture", "Manhattan Venture"), ("fundrise", "Fundrise"),
    ("greycroft", "Greycroft"), ("ark", "ARK"),
]

# Underlying companies people build SPVs around. token (normalised, space-
# separated) → (canonical name, slug). The slug must match the Form D / N-PORT
# company id so the SPV joins to an existing dossier.
_SPV_TARGETS = [
    ("anthropic", "Anthropic", "anthropic"),
    ("openai", "OpenAI", "openai"),
    ("open ai", "OpenAI", "openai"),
    ("spacex", "SpaceX", "spacex"),
    ("space exploration", "SpaceX", "spacex"),
    ("stripe", "Stripe", "stripe"),
    ("databricks", "Databricks", "databricks"),
    ("xai", "xAI", "xai"),
    ("scaleai", "Scale AI", "scale-ai"),
    ("scale ai", "Scale AI", "scale-ai"),
    ("anduril", "Anduril", "anduril"),
    ("ramp", "Ramp", "ramp"),
    ("mistral", "Mistral", "mistral"),
    ("perplexity", "Perplexity", "perplexity"),
    ("canva", "Canva", "canva"),
    ("discord", "Discord", "discord"),
    ("revolut", "Revolut", "revolut"),
    ("figure ai", "Figure AI", "figure-ai"),
    ("chime", "Chime", "chime"),
    ("plaid", "Plaid", "plaid"),
    ("rippling", "Rippling", "rippling"),
    ("deel", "Deel", "deel"),
    ("groq", "Groq", "groq"),
    ("cohere", "Cohere", "cohere"),
    ("epic games", "Epic Games", "epic-games"),
    ("fanatics", "Fanatics", "fanatics"),
    ("kraken", "Kraken", "kraken"),
    ("brex", "Brex", "brex"),
    ("notion", "Notion", "notion"),
    ("airtable", "Airtable", "airtable"),
    ("gusto", "Gusto", "gusto"),
    ("netskope", "Netskope", "netskope"),
    ("waymo", "Waymo", "waymo"),
    ("celonis", "Celonis", "celonis"),
    ("flexport", "Flexport", "flexport"),
]

# Tokens marking the filer as an SPV/feeder, not the operating company itself.
# Kept narrow (no bare "llc"/"lp") so an operating company that merely shares a
# target token isn't misread as an SPV.
_SPV_INDICATORS = (
    "spv", "fund", "series", "opportunit", "co invest", "coinvest", "feeder",
    "access",
)


def _norm_name(s):
    """Lowercase, strip punctuation to single spaces, pad with edge spaces so
    whole-token containment checks (' anthropic ') don't match substrings."""
    body = re.sub(r"\s+", " ", re.sub(r"[^a-z0-9]+", " ", (s or "").lower()).strip())
    return " " + body + " "


def _resolve_spv(name):
    """Return {underlying_name, underlying_id, sponsor} if `name` looks like an
    SPV targeting a known private company, else None."""
    low = _norm_name(name)
    target = None
    for tok, canon, slug in _SPV_TARGETS:
        if (" " + tok + " ") in low:
            target = (canon, slug)
            break
    if not target:
        return None
    sponsor = ""
    for tok, label in _SPV_SPONSORS:
        if low.startswith(" " + tok + " ") or (" " + tok + " ") in low:
            sponsor = label
            break
    has_indicator = any(ind in low for ind in _SPV_INDICATORS)
    # Require an SPV signal so the target's OWN operating Form D (e.g.
    # "Space Exploration Technologies Corp") isn't misclassified as interest
    # in itself.
    if not sponsor and not has_indicator:
        return None
    return {"underlying_name": target[0], "underlying_id": target[1], "sponsor": sponsor}


def build_spv_activity(filings, parse_xml_max=24):
    """Scan an already-fetched Form D filing list for SPVs targeting known
    private companies. Reuses the universe builder's search results (no extra
    EDGAR search requests); only matched SPVs incur an XML fetch, capped."""
    out = []
    parsed = 0
    seen = set()
    for f in filings:
        names = f.get("display_names") or []
        ciks = f.get("ciks") or []
        if not names or not ciks:
            continue
        raw = re.sub(r"\s*\(CIK.*$", "", names[0]).strip()
        res = _resolve_spv(raw)
        if not res:
            continue
        cik = ciks[0]
        key = (_cik_padded(cik), f.get("adsh", ""))
        if key in seen:
            continue
        seen.add(key)
        rec = {
            "underlying_id": res["underlying_id"],
            "underlying_name": res["underlying_name"],
            "sponsor": res["sponsor"],
            "spv_name": raw,
            "cik": _cik_padded(cik),
            "filed_date": f.get("filed_date", ""),
            "amount_sold_m": 0.0,
            "amount_offered_m": 0.0,
            "minimum_investment_usd": 0.0,
            "num_investors": 0,
            "edgar_url": "",
        }
        if parsed < parse_xml_max:
            x = fetch_form_d_xml(cik, f["adsh"])
            parsed += 1
            if x:
                rec["amount_sold_m"] = (x.get("total_sold_usd", 0) or 0) / 1_000_000.0
                rec["amount_offered_m"] = (x.get("total_offering_usd", 0) or 0) / 1_000_000.0
                rec["minimum_investment_usd"] = x.get("minimum_investment_usd", 0)
                rec["num_investors"] = x.get("already_invested_count", 0)
                rec["edgar_url"] = x.get("edgar_url", "")
        out.append(rec)
    out.sort(key=lambda r: r.get("filed_date", ""), reverse=True)
    return out


# ── EDGAR full-text search ─────────────────────────────────────────────────────

def search_filings(forms, days_back, max_hits=200, q=None):
    """List filings of given forms (D, S-1, F-1, S-11) over a date window.

    `q` runs an EDGAR full-text query (phrase-quoted by the caller); used to
    pull every SPV that mentions a target company across the full history,
    not just whatever shows up in the recent unfiltered feed."""
    start = (date.today() - timedelta(days=days_back)).isoformat()
    end = date.today().isoformat()
    out = []
    fetched = 0
    from_ = 0
    while fetched < max_hits:
        params = {
            "forms": ",".join(forms),
            "dateRange": "custom",
            "startdt": start,
            "enddt": end,
            "from": from_,
        }
        if q:
            params["q"] = q
        r = _get(EDGAR_SEARCH, params=params)
        if r is None:
            break
        try:
            data = r.json()
        except Exception:
            break
        hits = data.get("hits", {}).get("hits", [])
        if not hits:
            break
        for h in hits:
            src = h.get("_source", {})
            adsh = h.get("_id", "")
            if ":" in adsh:
                adsh = adsh.split(":", 1)[0]
            # EDGAR returns either "form" (singular) or "forms" (list) depending
            # on endpoint version. Prefer singular, then first of list, then
            # empty.
            form_val = src.get("form")
            if not form_val:
                forms_list = src.get("forms")
                if isinstance(forms_list, list) and forms_list:
                    form_val = forms_list[0]
                else:
                    form_val = ""
            out.append({
                "adsh": adsh,
                "form": form_val,
                "filed_date": src.get("file_date") or src.get("filed_date", ""),
                "display_names": src.get("display_names", []),
                "ciks": src.get("ciks", []),
            })
            fetched += 1
            if fetched >= max_hits:
                break
        if len(hits) < 10:
            break
        from_ += len(hits)
    return out


# Full-text query terms, tuned to how SPVs actually reference each company in
# filings — which is NOT always the display name. Empirically: SPVs write
# "Space Exploration" (not "SpaceX"), "ScaleAI" (not "Scale AI"); and EDGAR FTS
# returns nothing for a quoted lone word like "Stripe", so single words are
# sent unquoted while multi-word terms are phrase-quoted. Noisy hits from
# generic terms are harmless — _resolve_spv still gates on the entity name.
_SPV_QUERIES = [
    "Anthropic", "OpenAI", "Space Exploration", "ScaleAI", "xAI",
    "Stripe", "Databricks", "Anduril", "Brex", "Cohere", "Groq",
    "Discord", "Canva", "Chime", "Figure AI", "Fanatics", "Mistral",
    "Perplexity", "Kraken", "Revolut", "Rippling", "Deel", "Plaid",
    "Netskope", "Waymo", "Celonis", "Flexport", "Notion", "Airtable",
    "Gusto", "Epic Games",
]


def search_spv_filings(days_back=1825, per_target_hits=30):
    """Targeted EDGAR full-text searches — one query per tracked target — to
    surface SPVs across the full filing history rather than only the recent
    unfiltered window the universe builder samples. Public efts endpoint, no
    API key. Resolution to the canonical company is still done by _resolve_spv
    on each result's entity name."""
    out = []
    seen = set()
    for term in _SPV_QUERIES:
        t = term.strip()
        if not t or t.lower() in seen:
            continue
        seen.add(t.lower())
        q = ('"%s"' % t) if " " in t else t
        out.extend(search_filings(["D", "D/A"], days_back=days_back,
                                  max_hits=per_target_hits, q=q))
    return out


# ── Form D primary_doc.xml parser ──────────────────────────────────────────────

# Form D XML lives without an XMLSchema namespace on most filings, but newer
# ones do declare one (http://www.sec.gov/edgar/formd). Strip namespaces to keep
# tag lookups simple.
_NS_RE = re.compile(r"^\{[^}]+\}")


def _strip_ns(tree):
    for el in tree.iter():
        el.tag = _NS_RE.sub("", el.tag)


def fetch_form_d_xml(cik, adsh):
    """Fetch and parse a single Form D filing's primary_doc.xml."""
    cik_u = _cik_unpadded(cik)
    adsh_clean = adsh.replace("-", "")
    url = f"{EDGAR_ARCHIVE}/{cik_u}/{adsh_clean}/primary_doc.xml"
    r = _get(url, headers=UA_ARCHIVE)
    if r is None:
        return None
    try:
        root = ET.fromstring(r.content)
    except ET.ParseError:
        return None
    _strip_ns(root)

    def _text(path, default=""):
        el = root.find(path)
        return el.text.strip() if (el is not None and el.text) else default

    def _texts(path):
        return [el.text.strip() for el in root.findall(path) if el is not None and el.text]

    primary = root.find(".//primaryIssuer")
    if primary is None:
        return None

    addr = primary.find("issuerAddress")
    city = (addr.findtext("city", default="") if addr is not None else "").strip()
    state_code = (addr.findtext("stateOrCountry", default="") if addr is not None else "").strip()
    state_desc = (addr.findtext("stateOrCountryDescription", default="") if addr is not None else "").strip()
    zip_code = (addr.findtext("zipCode", default="") if addr is not None else "").strip()

    offering = root.find(".//offeringData")
    industry_group = ""
    federal_exemptions = []
    total_offering = 0.0
    total_sold = 0.0
    minimum_inv = 0.0
    securities_type = []
    nonaccr = 0
    invested_count = 0
    sales_commissions = 0.0
    use_of_proceeds = ""
    first_sale = ""
    if offering is not None:
        ig = offering.find(".//industryGroup/industryGroupType")
        if ig is not None and ig.text:
            industry_group = ig.text.strip()
        federal_exemptions = _texts(".//federalExemptionsExclusions/item")
        try:
            total_offering = float(offering.findtext(".//totalOfferingAmount", default="0") or 0)
        except (TypeError, ValueError):
            total_offering = 0.0
        try:
            total_sold = float(offering.findtext(".//totalAmountSold", default="0") or 0)
        except (TypeError, ValueError):
            total_sold = 0.0
        try:
            minimum_inv = float(offering.findtext(".//minimumInvestmentAccepted", default="0") or 0)
        except (TypeError, ValueError):
            minimum_inv = 0.0
        try:
            invested_count = int(offering.findtext(".//totalNumberAlreadyInvested", default="0") or 0)
        except (TypeError, ValueError):
            invested_count = 0
        # hasNonAccreditedInvestors is a boolean string ("true"/"false"), not
        # a count. The previous code tried to int() it which silently failed
        # and stored 0.
        has_non_accr_raw = (offering.findtext(".//hasNonAccreditedInvestors", default="") or "").strip().lower()
        has_non_accr = has_non_accr_raw == "true"
        nonaccr = 0  # Form D doesn't disclose the *count* of non-accredited, only the flag
        # Form D securities-type tags are camelCase booleans like
        # <isEquityType>true</isEquityType>, <isDebtType>false</isDebtType>.
        # Map to human-readable labels (the previous code shipped the raw tag
        # strings to the UI).
        _SEC_TYPE_LABELS = {
            "isEquityType":           "Equity",
            "isDebtType":             "Debt",
            "isOptionToAcquireType":  "Option",
            "isSecurityToBeAcquiredType": "Option",
            "isPooledInvestmentFundType": "Pooled Fund",
            "isTenantInCommonType":   "TIC",
            "isMineralPropertyType":  "Mineral",
            "isOtherType":            "Other",
        }
        sec_types = offering.find(".//typesOfSecuritiesOffered")
        if sec_types is not None:
            securities_type = [
                _SEC_TYPE_LABELS.get(el.tag, el.tag)
                for el in sec_types
                if el.text and el.text.strip().lower() == "true"
            ]
        try:
            sales_commissions = float(offering.findtext(".//salesCommissions/dollarAmount", default="0") or 0)
        except (TypeError, ValueError):
            sales_commissions = 0.0
        use_of_proceeds = (
            offering.findtext(".//useOfProceeds/clarificationOfResponse", default="") or ""
        ).strip()
        first_sale = (
            offering.findtext(".//dateOfFirstSale/value", default="") or
            offering.findtext(".//dateOfFirstSale", default="") or ""
        ).strip()

    related = []
    for rp in root.findall(".//relatedPersonInfo"):
        name = " ".join(filter(None, [
            (rp.findtext(".//relatedPersonName/firstName", default="") or "").strip(),
            (rp.findtext(".//relatedPersonName/middleName", default="") or "").strip(),
            (rp.findtext(".//relatedPersonName/lastName", default="") or "").strip(),
        ])).strip()
        rel_types = [t.text.strip() for t in rp.findall(".//relatedPersonRelationshipList/relationship") if t.text]
        if name:
            related.append({"name": name, "roles": rel_types})

    yoi = (primary.findtext("yearOfIncorporation/value", default="") or
           primary.findtext("yearOfIncorporation", default="")).strip()
    if yoi == "OverFiveYearsAgo":
        yoi = "older"

    return {
        "cik": _cik_padded(cik),
        "name": (primary.findtext("entityName", default="") or "").strip(),
        "city": city,
        "state": state_code,
        "state_desc": state_desc,
        "zip": zip_code,
        "year_of_incorporation": yoi,
        "industry_group": industry_group,
        "federal_exemptions": federal_exemptions,
        "total_offering_usd": total_offering,
        "total_sold_usd": total_sold,
        "minimum_investment_usd": minimum_inv,
        "has_non_accredited_investors": has_non_accr,
        "non_accredited_count": nonaccr,         # always 0 — Form D doesn't disclose
        "already_invested_count": invested_count,
        "securities_types": securities_type,
        "sales_commissions_usd": sales_commissions,
        "use_of_proceeds": use_of_proceeds,
        "first_sale_date": first_sale,
        "related_persons": related,
        "edgar_url": f"{EDGAR_ARCHIVE}/{_cik_unpadded(cik)}/{adsh.replace('-', '')}/primary_doc.xml",
    }


# ── Build company universe from recent Form Ds ─────────────────────────────────

_OPERATING_SECTORS_EXCLUDE = {
    # Form D industryGroup values we treat as non-operating-company. Pre-IPO
    # users care about portfolio companies, not the funds that hold them.
    "Pooled Investment Fund",
    "Hedge Fund",
    "Private Equity Fund",
    "Venture Capital Fund",
    "Other Investment Fund",
}


def _is_operating_company(industry_group):
    if not industry_group:
        return True  # keep the row if we don't know — better than dropping it
    return industry_group not in _OPERATING_SECTORS_EXCLUDE


def _filings_for_cik(cik, forms=("D", "D/A"), limit=10):
    """Fetch a CIK's recent filings of a given form set from data.sec.gov/submissions.

    Logs a stderr line on 404 so misspelled CIKs in KNOWN_PRIVATE_CIKS are
    visible during development rather than silently masked by `r is None`.
    """
    cik_pad = _cik_padded(cik)
    url = f"{EDGAR_SUBMISSIONS}/CIK{cik_pad}.json"
    r = _get(url, headers={**UA_ARCHIVE, "Host": "data.sec.gov"})
    if r is None:
        # _get already swallowed the exception, but for known-CIK lookups
        # surface it on stderr so the operator notices a stale curated entry.
        print(f"sec_form_d_data: submissions lookup failed for CIK {cik_pad}",
              file=sys.stderr)
        return []
    try:
        data = r.json()
    except Exception:
        print(f"sec_form_d_data: invalid JSON for CIK {cik_pad}", file=sys.stderr)
        return []
    recent = data.get("filings", {}).get("recent", {})
    forms_ = recent.get("form", []) or []
    adshes = recent.get("accessionNumber", []) or []
    dates  = recent.get("filingDate", []) or []
    forms_set = set(forms)
    out = []
    for i, f in enumerate(forms_):
        if f in forms_set:
            out.append({
                "adsh":       adshes[i] if i < len(adshes) else "",
                "filed_date": dates[i]  if i < len(dates)  else "",
                "form":       f,
                "ciks":       [cik],
                "display_names": [data.get("name") or ""],
            })
            if len(out) >= limit:
                break
    return out


def fetch_form_d_filings(days_back=180, max_filings=120, include_known=True):
    """Fetch the raw Form D filing list (curated CIKs + recent EDGAR-wide
    search). Pulled out so the universe builder (build_form_d_companies) and the
    flat recent-filings feed share one search result set instead of issuing it
    twice. (The deep SPV scan is a separate action with its own targeted
    full-text queries — it does not reuse this list.)"""
    filings = []
    if include_known:
        for cik in KNOWN_PRIVATE_CIKS:
            filings.extend(_filings_for_cik(cik, ("D", "D/A"), limit=8))
    filings.extend(search_filings(["D", "D/A"], days_back=days_back, max_hits=max_filings))
    return filings


def build_form_d_companies(days_back=180, max_filings=120, parse_xml_max=80,
                            filter_pooled_funds=True, include_known=True,
                            filings=None):
    """Build the pre-IPO universe.

    Strategy:
      1. Fetch each curated CIK's recent Form D filings directly.
      2. Backfill with recent EDGAR-wide Form D search, filtering out
         pooled-investment-fund filers (VC/PE funds raising from LPs).

    `filings` may be supplied by the caller to reuse a shared search result set.
    """
    if filings is None:
        filings = fetch_form_d_filings(days_back=days_back, max_filings=max_filings,
                                       include_known=include_known)

    companies = {}
    recent_flat = []
    parsed = 0
    for f in filings:
        ciks = f.get("ciks") or []
        names = f.get("display_names") or []
        if not ciks or not names:
            continue
        cik = ciks[0]
        display = names[0]
        cik_pad = _cik_padded(cik)
        cid = _slug(display) or cik_pad

        parsed_xml = None
        if parsed < parse_xml_max:
            parsed_xml = fetch_form_d_xml(cik, f["adsh"])
            parsed += 1
            # Drop pooled-investment-fund filers (they're VC/PE funds, not portfolio companies).
            if filter_pooled_funds and parsed_xml and \
               not _is_operating_company(parsed_xml.get("industry_group", "")):
                continue

        if cid not in companies:
            companies[cid] = {
                "id": cid,
                "cik": cik_pad,
                "name": (parsed_xml or {}).get("name") or re.sub(r"\s*\(CIK.*$", "", display).strip(),
                "industry_group": (parsed_xml or {}).get("industry_group", ""),
                "city": (parsed_xml or {}).get("city", ""),
                "state": (parsed_xml or {}).get("state", ""),
                "year_of_incorporation": (parsed_xml or {}).get("year_of_incorporation", ""),
                "rounds": [],
                "cumulative_raised_m": 0.0,
            }
        c = companies[cid]
        if parsed_xml:
            for fld in ("industry_group", "city", "state", "year_of_incorporation"):
                if not c[fld] and parsed_xml.get(fld):
                    c[fld] = parsed_xml[fld]
            amount_m = (parsed_xml.get("total_sold_usd", 0) or 0) / 1_000_000.0
            c["cumulative_raised_m"] += amount_m
            round_entry = {
                "adsh": f["adsh"],
                "filed_date": f.get("filed_date", ""),
                "first_sale_date": parsed_xml.get("first_sale_date", ""),
                "amount_m": amount_m,
                "offering_m": (parsed_xml.get("total_offering_usd", 0) or 0) / 1_000_000.0,
                "exemption": ", ".join(parsed_xml.get("federal_exemptions", []) or []),
                "securities_types": parsed_xml.get("securities_types", []),
                "minimum_investment_usd": parsed_xml.get("minimum_investment_usd", 0),
                "related_persons": parsed_xml.get("related_persons", []),
                "edgar_url": parsed_xml.get("edgar_url", ""),
            }
            c["rounds"].append(round_entry)
            recent_flat.append({
                "company_name": c["name"],
                "cik": cik_pad,
                "filed_date": f.get("filed_date", ""),
                "amount_raised": amount_m,
                "exemption": round_entry["exemption"],
                "offering_type": ", ".join(parsed_xml.get("securities_types", []) or []) or "Equity",
                "state": parsed_xml.get("state", ""),
                "edgar_url": parsed_xml.get("edgar_url", ""),
            })
        else:
            recent_flat.append({
                "company_name": c["name"],
                "cik": cik_pad,
                "filed_date": f.get("filed_date", ""),
                "amount_raised": 0,
                "exemption": "",
                "offering_type": "Equity",
                "state": "",
                "edgar_url": "",
            })

    # Sort rounds within each company by filed_date desc; sort companies by cumulative raised desc.
    for c in companies.values():
        c["rounds"].sort(key=lambda r: r.get("filed_date", ""), reverse=True)
    company_list = sorted(companies.values(), key=lambda c: c.get("cumulative_raised_m", 0), reverse=True)
    return company_list, recent_flat


# ── S-1 pipeline (delegated to sec_s1_pipeline.py for richer parsing; we still
# expose a minimal version here so PreIpoService can fall back to a single
# Python invocation if needed). ────────────────────────────────────────────────

def fetch_ipo_pipeline(days_back=120, max_hits=80):
    filings = search_filings(["S-1", "S-1/A", "F-1", "F-1/A", "S-11", "S-11/A"], days_back=days_back, max_hits=max_hits)
    # Sort by filed_date ASC so that for each CIK we encounter the *original*
    # filing first (if it's in our window); subsequent amendments then bump
    # the counter without ever underflowing or mislabelling the first filed
    # date. The previous code processed in EDGAR-search order (newest-first),
    # so an initial filing arriving after an amendment in the same window
    # corrupted the count.
    filings.sort(key=lambda x: x.get("filed_date", ""))

    out = []
    seen = {}  # cik -> first index in out
    for f in filings:
        ciks = f.get("ciks") or []
        names = f.get("display_names") or []
        if not ciks or not names:
            continue
        cik = _cik_padded(ciks[0])
        name = re.sub(r"\s*\(CIK.*$", "", names[0]).strip()
        is_amendment = "/A" in (f.get("form") or "")
        if cik in seen:
            idx = seen[cik]
            if is_amendment:
                out[idx]["amendment_count"] += 1
                if (f.get("filed_date") or "") > (out[idx]["latest_amended"] or ""):
                    out[idx]["latest_amended"] = f.get("filed_date", "")
            # If we now meet a non-amendment for a CIK already seen, leave
            # first_filed as the earliest-seen filing — same value, no change.
        else:
            seen[cik] = len(out)
            out.append({
                "company_name": name,
                "cik": cik,
                "filed_date": f.get("filed_date", ""),
                "first_filed": f.get("filed_date", ""),
                "latest_amended": f.get("filed_date", "") if is_amendment else "",
                "amendment_count": 1 if is_amendment else 0,
                "form_type": f.get("form", ""),
                "edgar_url": f"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK={cik}&type=S-1",
                "is_amendment": is_amendment,
            })
    return out


# ── Action dispatch ────────────────────────────────────────────────────────────

def build_all_data(days_back_fd=180, days_back_ipo=120, max_filings=120, parse_xml_max=80,
                   filter_pooled_funds=True, include_known=True):
    filings = fetch_form_d_filings(days_back=days_back_fd, max_filings=max_filings,
                                   include_known=include_known)
    companies, recent_flat = build_form_d_companies(
        parse_xml_max=parse_xml_max, filter_pooled_funds=filter_pooled_funds,
        filings=filings,
    )
    pipeline = fetch_ipo_pipeline(days_back=days_back_ipo, max_hits=80)
    return {
        "form_d_companies": companies,
        "recent_form_d": recent_flat,
        "s1_filings": pipeline,
        "as_of": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }


def spv_query_counts():
    """Data-based change signal: one page-1 request per SPV query returning
    EDGAR's all-time total hit count (`hits.total.value`). No paging, no XML.

    Counts are ALL-TIME on purpose — a sliding date window would change every
    day as old filings age off the back, defeating the "did anything change?"
    test. All-time totals only ever grow when a genuinely new SPV files, so an
    unchanged map means there is nothing new to fetch."""
    counts = {}
    for term in _SPV_QUERIES:
        t = term.strip()
        if not t or t.lower() in counts:
            continue
        q = ('"%s"' % t) if " " in t else t
        r = _get(EDGAR_SEARCH, params={"forms": "D,D/A", "q": q, "from": 0})
        total = 0
        if r is not None:
            try:
                total = int(r.json().get("hits", {}).get("total", {}).get("value", 0) or 0)
            except Exception:
                total = 0
        counts[t.lower()] = total
    return counts


def build_spv_deep(spv_days_back=1825, spv_hits_per_target=30, spv_parse_max=40,
                   prev_cursor=None):
    """Deep SPV scan, gated by a data-based cursor. Runs the cheap all-time
    count probe first; if the counts match the prior cursor, returns
    {"unchanged": true} without the expensive search+XML. Run as its OWN fetch
    so even a full scan never jeopardises the core Form D load."""
    counts = spv_query_counts()
    prev = prev_cursor.get("counts") if isinstance(prev_cursor, dict) else None
    # any(counts.values()): a degenerate all-zero probe (every count request
    # failed — an EDGAR outage) must not be trusted as "unchanged"; it would
    # match a prior failed cursor and suppress the scan. Require a real signal.
    if prev is not None and counts == prev and any(counts.values()):
        return {"unchanged": True, "cursor": {"counts": counts}}

    filings = search_spv_filings(days_back=spv_days_back,
                                 per_target_hits=spv_hits_per_target)
    # Newest-first so the capped XML-enrichment budget lands on the most recent
    # SPVs; build_spv_activity dedups keeping first occurrence.
    filings.sort(key=lambda f: f.get("filed_date", ""), reverse=True)
    return {
        "spv_activity": build_spv_activity(filings, parse_xml_max=spv_parse_max),
        "cursor": {"counts": counts},
        "as_of": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }


def handle_action(action, payload):
    if action == "all_data":
        return build_all_data(
            days_back_fd=payload.get("days_back_fd", 180),
            days_back_ipo=payload.get("days_back_ipo", 120),
            max_filings=payload.get("max_filings", 120),
            parse_xml_max=payload.get("parse_xml_max", 200),
            filter_pooled_funds=payload.get("filter_pooled_funds", True),
            include_known=payload.get("include_known", True),
        )
    if action == "spv_deep":
        return build_spv_deep(
            spv_days_back=payload.get("spv_days_back", 1825),
            spv_hits_per_target=payload.get("spv_hits_per_target", 30),
            spv_parse_max=payload.get("spv_parse_max", 40),
            prev_cursor=payload.get("prev_cursor"),
        )
    if action == "form_d_recent":
        days = payload.get("days_back", 30)
        max_filings = payload.get("max_filings", 60)
        _, recent = build_form_d_companies(days_back=days, max_filings=max_filings, parse_xml_max=0)
        return recent
    if action == "company_rounds":
        cik = payload.get("cik")
        if not cik:
            return {"error": "cik required"}
        days = payload.get("days_back", 3 * 365)
        filings = search_filings(["D", "D/A"], days_back=days, max_hits=60)
        rounds = []
        for f in filings:
            ciks = f.get("ciks") or []
            if not ciks or _cik_padded(ciks[0]) != _cik_padded(cik):
                continue
            x = fetch_form_d_xml(cik, f["adsh"])
            if x:
                rounds.append({
                    "filed_date": f.get("filed_date", ""),
                    "amount_m": (x.get("total_sold_usd", 0) or 0) / 1_000_000.0,
                    "exemption": ", ".join(x.get("federal_exemptions", []) or []),
                    "related_persons": x.get("related_persons", []),
                    "edgar_url": x.get("edgar_url", ""),
                })
        return rounds
    if action == "ipo_pipeline":
        return fetch_ipo_pipeline(days_back=payload.get("days_back", 120), max_hits=payload.get("max_hits", 80))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "all_data"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(action, payload), indent=2, default=str))
