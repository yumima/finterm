#!/usr/bin/env python3
"""
SEC ownership data — Form 4 insider transactions and 5% beneficial-owner stakes.

Powers the OWNERSHIP screen. Everything here comes from EDGAR: public, free, no
API key. EDGAR asks for a descriptive User-Agent and <=10 req/s shared across it.

WHY THIS SCRIPT EXISTS
    sec_data.py already had get_insider_trading() and get_institutional_ownership(),
    but both were placeholders that returned FABRICATED rows — a hardcoded
    transaction_type of "P", zeroed prices, and the literal string
    "Extracted from filing" for every name — while reporting success: True.
    Anything consuming them (including the AI chat, which can run any script via
    run_python_script) was being handed invented data that looked real. This
    script does the parse for real; the stubs now delegate here.

WHAT IS AND IS NOT COMPUTED
    Nothing is inferred that the filing does not state. Where a value is absent
    from the XML it is omitted from the payload rather than defaulted, so the UI
    renders a placeholder instead of a confident zero. The routine/opportunistic
    classification is only emitted when there is enough filing history to
    support it; otherwise the insider is reported as "unclassified" and the
    caller is told why.

ACTIONS
    insiders   {"symbol", "months"?, "max_filings"?}  Form 4 transactions
    stakes     {"symbol", "months"?}                  SC 13D / 13G filings
    all        {"symbol", "months"?, "max_filings"?}  both, one round trip
"""

import json
import re
import sys
import time
from collections import defaultdict
from datetime import date, datetime, timedelta
from xml.etree import ElementTree

try:
    import requests
except ImportError:
    requests = None

UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
}

# Same 0.4s floor the other SEC scripts use: <=10 req/s is shared across the
# User-Agent, and several SEC scripts can be in flight at once.
_LAST_REQ = 0.0
_MIN_REQ_GAP = 0.4

# Form 4 transaction codes. Only P and S are open-market decisions; the rest are
# compensation mechanics or administrative. Splitting them is the whole point —
# a grant vesting is not a director choosing to buy.
OPEN_MARKET = {"P", "S"}
CODE_LABEL = {
    "P": "Purchase", "S": "Sale", "A": "Grant/Award", "D": "Disposition to issuer",
    "F": "Tax withholding", "M": "Option exercise", "G": "Gift",
    "C": "Conversion", "E": "Short expiration", "H": "Long expiration",
    "I": "Discretionary", "J": "Other", "K": "Equity swap", "L": "Small acquisition",
    "U": "Tender", "V": "Early report", "W": "Will/descent", "X": "In-the-money exercise",
    "Z": "Voting trust",
}

# Cohen/Malloy/Pomorski separate insiders who trade the same calendar month every
# year ("routine") from everyone else ("opportunistic"), and find the predictive
# content sits with the latter. Their threshold is 3 consecutive years of a trade
# in the same month.
ROUTINE_MIN_YEARS = 3
# A cluster is >=2 distinct insiders buying on the open market inside this window.
CLUSTER_WINDOW_DAYS = 30
CLUSTER_MIN_INSIDERS = 2


def _get(url, headers=None, timeout=20):
    """Throttled GET. Returns the response or None — never raises."""
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < _MIN_REQ_GAP:
        time.sleep(_MIN_REQ_GAP - elapsed)
    try:
        r = requests.get(url, timeout=timeout, headers=headers or UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        _LAST_REQ = time.time()
        return None


def _pad_cik(cik):
    return str(cik).lstrip("0").zfill(10)


def _norm_form(form):
    """Canonical form name.

    EDGAR carries the beneficial-ownership schedules under two spellings in the
    same submissions index: the historical "SC 13D/A" and the "SCHEDULE 13D/A"
    that the post-2024 structured filings use. GameStop's index has all four
    variants at once. Matching only one spelling silently drops the newer
    filings, which reads as "no activist stakes" on an issuer that has several.
    """
    f = (form or "").upper().strip()
    if f.startswith("SCHEDULE "):
        return "SC " + f[len("SCHEDULE "):]
    return f


_TICKER_MAP = None


def cik_for_symbol(symbol):
    """Resolve a ticker to a zero-padded CIK, or None when EDGAR does not list it."""
    global _TICKER_MAP
    if _TICKER_MAP is None:
        r = _get("https://www.sec.gov/files/company_tickers.json")
        if r is None:
            return None
        try:
            raw = r.json()
        except Exception:
            return None
        _TICKER_MAP = {}
        for row in raw.values():
            t = str(row.get("ticker", "")).upper()
            if t:
                _TICKER_MAP[t] = (_pad_cik(row.get("cik_str", "")), row.get("title", ""))
    hit = _TICKER_MAP.get(str(symbol).upper())
    return hit[0] if hit else None


def company_name_for_symbol(symbol):
    cik_for_symbol(symbol)  # populates the map
    hit = (_TICKER_MAP or {}).get(str(symbol).upper())
    return hit[1] if hit else ""


def _submissions(cik):
    r = _get(f"https://data.sec.gov/submissions/CIK{_pad_cik(cik)}.json")
    if r is None:
        return None
    try:
        return r.json()
    except Exception:
        return None


def recent_filings(cik, forms, since_date):
    """Filings of the given form types filed on/after since_date.

    The submissions endpoint splits history: `filings.recent` holds roughly the
    last year or 1000 filings, and older ones live in separate files listed
    under `filings.files`. An issuer with heavy Form 4 traffic can push a year
    of history out of `recent`, so the overflow files are pulled too whenever
    their advertised date range still overlaps the window asked for.
    """
    subs = _submissions(cik)
    if not subs:
        return []
    wanted = {_norm_form(f) for f in forms}
    out = []

    def harvest(block):
        acc = block.get("accessionNumber") or []
        frm = block.get("form") or []
        fdt = block.get("filingDate") or []
        pdoc = block.get("primaryDocument") or []
        rdt = block.get("reportDate") or []
        for i in range(len(acc)):
            form = _norm_form(frm[i] if i < len(frm) else "")
            if form not in wanted:
                continue
            filed = fdt[i] if i < len(fdt) else ""
            if filed < since_date:
                continue
            out.append({
                "accession": acc[i],
                "form": form,
                "filed_date": filed,
                "report_date": rdt[i] if i < len(rdt) else "",
                "primary_doc": pdoc[i] if i < len(pdoc) else "",
            })

    harvest((subs.get("filings") or {}).get("recent") or {})
    for extra in ((subs.get("filings") or {}).get("files") or []):
        # Each entry advertises the range it covers; skip files that end before
        # the window opens rather than downloading them to find out.
        if (extra.get("filingTo") or "9999-99-99") < since_date:
            continue
        name = extra.get("name")
        if not name:
            continue
        r = _get(f"https://data.sec.gov/submissions/{name}")
        if r is None:
            continue
        try:
            harvest(r.json())
        except Exception:
            continue
    return out


def _raw_xml_url(cik, accession, primary_doc):
    """URL of the machine-readable Form 4 XML.

    primaryDocument is often the XSL-rendered path ("xslF345X03/wf-form4_1.xml");
    the raw XML sits beside it with that prefix stripped.
    """
    acc = accession.replace("-", "")
    doc = re.sub(r"^xslF345X\d+/", "", primary_doc or "")
    if not doc.lower().endswith(".xml"):
        return None
    return f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc}/{doc}"


def _text(node, path):
    """Text of a child element, or None. Form 4 wraps most leaves in <value>."""
    if node is None:
        return None
    el = node.find(path)
    if el is None:
        return None
    val = el.find("value")
    target = val if val is not None else el
    return (target.text or "").strip() or None


def _num(node, path):
    t = _text(node, path)
    if t is None:
        return None
    try:
        return float(t.replace(",", ""))
    except ValueError:
        return None


def parse_form4(xml_bytes, source_url):
    """Parse one Form 4 into a list of transaction dicts.

    Absent fields are omitted, never defaulted. A price of 0 is a real filed
    value (a gift, a grant) and is preserved as 0; a price that is simply not
    reported is left out so the caller can tell the two apart.
    """
    try:
        root = ElementTree.fromstring(xml_bytes)
    except ElementTree.ParseError:
        return []

    issuer = root.find("issuer")
    issuer_cik = _text(issuer, "issuerCik") or ""
    issuer_name = _text(issuer, "issuerName") or ""
    issuer_symbol = _text(issuer, "issuerTradingSymbol") or ""

    owners = []
    for ro in root.findall("reportingOwner"):
        name = _text(ro, "reportingOwnerId/rptOwnerName") or ""
        owner_cik = _text(ro, "reportingOwnerId/rptOwnerCik") or ""
        rel = ro.find("reportingOwnerRelationship")
        roles = []
        if rel is not None:
            if (_text(rel, "isDirector") or "") in ("1", "true"):
                roles.append("Director")
            if (_text(rel, "isOfficer") or "") in ("1", "true"):
                roles.append(_text(rel, "officerTitle") or "Officer")
            if (_text(rel, "isTenPercentOwner") or "") in ("1", "true"):
                roles.append("10% owner")
            if (_text(rel, "isOther") or "") in ("1", "true"):
                roles.append(_text(rel, "otherText") or "Other")
        owners.append({"name": name, "cik": owner_cik, "roles": roles})

    owner_name = owners[0]["name"] if owners else ""
    owner_cik_first = owners[0]["cik"] if owners else ""
    owner_roles = owners[0]["roles"] if owners else []

    rows = []
    tables = [("nonDerivativeTable/nonDerivativeTransaction", False),
              ("derivativeTable/derivativeTransaction", True)]
    for path, is_deriv in tables:
        for tx in root.findall(path):
            coding = tx.find("transactionCoding")
            code = _text(coding, "transactionCode") if coding is not None else None
            amounts = tx.find("transactionAmounts")
            shares = _num(amounts, "transactionShares") if amounts is not None else None
            price = _num(amounts, "transactionPricePerShare") if amounts is not None else None
            ad = _text(amounts, "transactionAcquiredDisposedCode") if amounts is not None else None
            post = tx.find("postTransactionAmounts")
            held = _num(post, "sharesOwnedFollowingTransaction") if post is not None else None
            nature = tx.find("ownershipNature")
            direct = _text(nature, "directOrIndirectOwnership") if nature is not None else None

            row = {
                "insider": owner_name,
                "insider_cik": owner_cik_first,
                # Joint filings are common — a fund files alongside its GP or
                # managing member. Their names are carried for display, but
                # they are deliberately NOT counted as separate participants in
                # find_clusters: a fund and its managing member filing together
                # is ONE decision, and treating it as two would manufacture a
                # cluster out of a single filer.
                "co_filers": [o["name"] for o in owners[1:] if o["name"]],
                "roles": owner_roles,
                "issuer": issuer_name,
                "issuer_cik": issuer_cik,
                "symbol": issuer_symbol,
                "date": _text(tx, "transactionDate") or "",
                "code": code or "",
                "code_label": CODE_LABEL.get(code or "", code or ""),
                "security": _text(tx, "securityTitle") or "",
                "derivative": is_deriv,
                "open_market": (code or "") in OPEN_MARKET,
                "source_url": source_url,
            }
            if shares is not None:
                row["shares"] = shares
            if price is not None:
                row["price"] = price
            if shares is not None and price is not None:
                row["value"] = shares * price
            if ad:
                row["direction"] = "acquired" if ad == "A" else "disposed"
            if held is not None:
                row["shares_held_after"] = held
            if direct:
                row["ownership"] = "direct" if direct == "D" else "indirect"
            if row["date"]:
                rows.append(row)
    return rows


def owner_filing_dates(owner_cik, years=6):
    """Every Form 4 transaction date this person has reported, from their own
    submissions index.

    The classification needs years of history per insider, but the transaction
    table is necessarily capped — Ford filed 361 Form 4s in four years and
    parsing them all would take minutes. Taking the most recent N collapses the
    window to about one year, which left every insider permanently
    "unclassified" on exactly the large caps people look at.

    Each reporting owner has their own CIK, and their submissions index carries
    the period-of-report date for every Form 4 they have ever filed. So the full
    history costs ONE request per insider and no XML parsing at all, which is
    both cheaper and more complete than widening the issuer-side cap.
    """
    if not owner_cik:
        return []
    since = (date.today() - timedelta(days=365 * years)).isoformat()
    filings = recent_filings(owner_cik, ["4", "4/A"], since)
    # reportDate is the transaction date; filingDate is up to two business days
    # later and would smear a year-end trade into January.
    return [f["report_date"] for f in filings if f.get("report_date")]


def classify_insiders(transactions, date_lookup=None):
    """Split insiders into routine and opportunistic, or decline to.

    Routine means a trade in the same calendar month in >=ROUTINE_MIN_YEARS
    distinct years. That test needs multiple years of filings to be meaningful,
    so an insider whose history spans fewer years is reported as "unclassified"
    with the span that was available. Guessing would put a label on the majority
    of insiders that the data cannot support.

    `date_lookup` maps insider name -> full list of transaction dates. When
    given it replaces the dates visible in `transactions`, which are capped.
    """
    by_insider = defaultdict(list)
    for t in transactions:
        if t.get("insider") and t.get("date"):
            by_insider[t["insider"]].append(t["date"])

    out = []
    for name, local_dates in by_insider.items():
        dates = (date_lookup or {}).get(name) or local_dates
        years = set()
        months = defaultdict(set)
        for d in dates:
            try:
                y, m = int(d[0:4]), int(d[5:7])
            except (ValueError, IndexError):
                continue
            years.add(y)
            months[m].add(y)
        span = len(years)
        entry = {"insider": name, "trades": len(dates), "years_observed": span,
                 "trades_in_window": len(local_dates)}
        if span < ROUTINE_MIN_YEARS:
            entry["pattern"] = "unclassified"
            entry["reason"] = f"only {span} year(s) of filings in window"
        else:
            routine_month = max(
                (m for m, ys in months.items() if len(ys) >= ROUTINE_MIN_YEARS),
                key=lambda m: len(months[m]), default=None)
            if routine_month is not None:
                entry["pattern"] = "routine"
                entry["routine_month"] = routine_month
            else:
                entry["pattern"] = "opportunistic"
        out.append(entry)
    out.sort(key=lambda e: (-e["trades"], e["insider"]))
    return out


def find_clusters(transactions):
    """Windows where >=2 distinct insiders bought on the open market.

    Only code P counts. A cluster of grants vesting in the same week is a
    calendar artifact, not several people independently deciding to buy.
    """
    buys = sorted(
        (t for t in transactions if t.get("code") == "P" and t.get("date")),
        key=lambda t: t["date"])
    clusters = []
    for i, anchor in enumerate(buys):
        try:
            start = datetime.strptime(anchor["date"], "%Y-%m-%d").date()
        except ValueError:
            continue
        members, value, seen = {}, 0.0, set()
        for t in buys[i:]:
            try:
                d = datetime.strptime(t["date"], "%Y-%m-%d").date()
            except ValueError:
                continue
            if (d - start).days > CLUSTER_WINDOW_DAYS:
                break
            members[t["insider"]] = True
            seen.add(t["date"])
            value += t.get("value") or 0.0
        if len(members) >= CLUSTER_MIN_INSIDERS:
            clusters.append({
                "start": anchor["date"],
                "end": max(seen),
                "insiders": sorted(members.keys()),
                "insider_count": len(members),
                "total_value": value,
            })
    # Collapse clusters describing the same episode. Comparing only against the
    # LAST kept entry is not enough: {A,B} -> {A,C} -> {A,B} inside overlapping
    # windows leaves the third standing, because it is not a subset of {A,C}.
    # Every retained cluster has to be considered.
    merged = []
    for c in clusters:
        dup = False
        for kept in merged:
            overlaps = c["start"] <= kept["end"] and kept["start"] <= c["end"]
            if overlaps and set(c["insiders"]) <= set(kept["insiders"]):
                dup = True
                break
        if not dup:
            merged.append(c)
    # Newest first. Callers that show "the" cluster mean the most recent one —
    # an Elevated finding pointing at a buy from ten months ago while a fresh
    # one goes unmentioned is worse than showing nothing.
    merged.sort(key=lambda c: c["start"], reverse=True)
    return merged


def fetch_insiders(symbol, months=24, max_filings=80, cik=None):
    """Form 4 transactions for one issuer over the trailing `months`.

    `cik` short-circuits the ticker lookup. sec_data.py supports a CIK-only
    call — no symbol at all — and resolving "" to a CIK fails for a caller that
    already told us exactly which company it meant.
    """
    if requests is None:
        return {"error": "requests not available", "symbol": symbol}
    cik = _pad_cik(cik) if cik else cik_for_symbol(symbol)
    if not cik:
        return {"error": f"No EDGAR CIK for {symbol}", "symbol": symbol}

    since = (date.today() - timedelta(days=int(months * 30.44))).isoformat()
    filings = recent_filings(cik, ["4", "4/A"], since)
    filings.sort(key=lambda f: f.get("filed_date", ""), reverse=True)

    # Cap the fetch, but say so — a silently truncated window reads as a quiet
    # period when it is actually a busy one.
    total = len(filings)
    truncated = max(0, total - int(max_filings))
    filings = filings[:int(max_filings)]

    transactions, unparsed = [], 0
    # Rows dropped because this CIK filed them ABOUT someone else — reported
    # rather than silently discarded, since "Berkshire sold DaVita" is a real
    # filing a reader may have been looking for on the other company's page.
    filed_as_owner, other_issuers, parsed_for_issuer = 0, set(), 0
    for f in filings:
        url = _raw_xml_url(cik, f["accession"], f["primary_doc"])
        if not url:
            unparsed += 1
            continue
        r = _get(url, headers=UA)
        if r is None:
            unparsed += 1
            continue
        rows = parse_form4(r.content, url)
        if not rows:
            unparsed += 1
            continue

        # A CIK's submissions index carries every Form 4 the CIK is a PARTY to,
        # not only the ones where it is the issuer. A holding company that is a
        # 10% owner of other issuers files Form 4s about THEM, and those land
        # here looking like insider trades in this company at another company's
        # share price. Berkshire's 2026-08-04 filing is a DaVita sale at
        # $199.55; shown under BRK-B, whose own insiders were trading at $510,
        # it reads as bad data. It is good data about a different issuer.
        foreign = [row for row in rows
                   if row.get("issuer_cik") and _pad_cik(row["issuer_cik"]) != cik]
        if foreign:
            other_issuers.update(row.get("issuer", "") for row in foreign)
            filed_as_owner += len(foreign)
        rows = [row for row in rows
                if not row.get("issuer_cik") or _pad_cik(row["issuer_cik"]) == cik]
        if not rows:
            continue
        parsed_for_issuer += 1
        for row in rows:
            row["filed_date"] = f.get("filed_date", "")
            row["form"] = f.get("form", "4")
        transactions.extend(rows)

    transactions.sort(key=lambda t: (t.get("date", ""), t.get("insider", "")), reverse=True)

    # One request per distinct insider buys the full multi-year history the
    # routine/opportunistic test needs, without widening the filing cap above.
    owner_ciks = {}
    for t in transactions:
        if t.get("insider") and t.get("insider_cik"):
            owner_ciks.setdefault(t["insider"], t["insider_cik"])
    date_lookup = {}
    for name, ocik in owner_ciks.items():
        hist = owner_filing_dates(ocik)
        if hist:
            date_lookup[name] = hist

    return {
        "symbol": str(symbol).upper(),
        "cik": cik,
        "company": company_name_for_symbol(symbol),
        "window_months": months,
        "since": since,
        "filings_found": total,
        # Filings that yielded at least one row FOR THIS ISSUER. A holding
        # company's Form 4s parse fine and belong to somebody else; counting
        # them as parsed put "60 filings, 60 parsed" over a near-empty table.
        "filings_parsed": parsed_for_issuer,
        "filings_truncated": truncated,
        "filings_unparsed": unparsed,
        # Form 4 rows this CIK filed about OTHER issuers, dropped from the
        # table above because they are not insider trades in this company.
        "rows_filed_as_owner": filed_as_owner,
        "other_issuers": sorted(x for x in other_issuers if x),
        "transactions": transactions,
        "insiders": classify_insiders(transactions, date_lookup),
        "clusters": find_clusters(transactions),
    }


_PARTY_RE = re.compile(
    r"(SUBJECT COMPANY|FILED BY):(.*?)CENTRAL INDEX KEY:\s*(\d+)", re.S)
_NAME_RE = re.compile(r"COMPANY CONFORMED NAME:\s*(.+)")


def _filing_parties(cik, accession):
    """(subject_cik, filer_cik, filer_name) for one accession, or None.

    A beneficial-ownership schedule names both sides in the SGML header at the
    top of the full submission text file, and nowhere in the submissions JSON.
    Only the first few KB are needed, so this is a ranged GET — servers that
    ignore Range simply send more than was asked for, which still parses.
    """
    if not accession:
        return None
    url = "https://www.sec.gov/Archives/edgar/data/{}/{}/{}.txt".format(
        int(cik), accession.replace("-", ""), accession)
    r = _get(url, headers=dict(UA, Range="bytes=0-6000"))
    if r is None:
        # One retry. These run on top of the Form 4 fetches against the same
        # 10 req/s budget, so a transient 403 is expected — and treating it as
        # an answer would drop a real 13D from the list.
        time.sleep(_MIN_REQ_GAP)
        r = _get(url, headers=dict(UA, Range="bytes=0-6000"))
    if r is None:
        return None
    text = r.text or ""
    subject_cik, filer_cik, filer_name = "", "", ""
    for role, block, party_cik in _PARTY_RE.findall(text):
        name_hit = _NAME_RE.search(block)
        name = name_hit.group(1).strip() if name_hit else ""
        if role == "SUBJECT COMPANY":
            subject_cik = _pad_cik(party_cik)
        elif not filer_cik:
            filer_cik, filer_name = _pad_cik(party_cik), name
    # A header with neither side named is not evidence of anything.
    if not subject_cik and not filer_cik:
        return None
    return subject_cik, filer_cik, filer_name


def fetch_stakes(symbol, months=24, cik=None, max_filings=80):
    """SC 13D / 13G beneficial-ownership filings against one issuer.

    Reports what the submissions index states — filer, form, dates, document.
    The percentage owned lives inside the filing body, which is free-form HTML
    for most filers, so it is deliberately NOT extracted here: a regex over
    prose would produce a number that looks authoritative and is sometimes
    wrong. The document link is provided instead.
    """
    if requests is None:
        return {"error": "requests not available", "symbol": symbol}
    cik = _pad_cik(cik) if cik else cik_for_symbol(symbol)
    if not cik:
        return {"error": f"No EDGAR CIK for {symbol}", "symbol": symbol}

    since = (date.today() - timedelta(days=int(months * 30.44))).isoformat()
    forms = ["SC 13D", "SC 13D/A", "SC 13G", "SC 13G/A"]
    filings = recent_filings(cik, forms, since)
    filings.sort(key=lambda f: f.get("filed_date", ""), reverse=True)
    total = len(filings)
    truncated = max(0, total - int(max_filings))
    filings = filings[:int(max_filings)]

    acc_dir = "https://www.sec.gov/Archives/edgar/data/{}/{}/{}"
    out, filed_by_this_cik, unverified = [], 0, 0
    for f in filings:
        form = f.get("form", "")
        # Same trap as Form 4: a CIK's submissions index lists the schedules it
        # FILED about other companies alongside the ones filed AGAINST it. A
        # 13G Berkshire filed on Delta Air Lines is not a stake in Berkshire.
        # The submissions JSON does not say which side this CIK is on, but the
        # submission header does, and it is the first few KB of the file.
        parties = _filing_parties(cik, f.get("accession", ""))
        if parties is None:
            unverified += 1          # counted, not guessed at
            continue
        subject_cik, filer_cik, filer_name = parties
        if subject_cik and subject_cik != cik:
            filed_by_this_cik += 1
            continue
        out.append({
            "form": form,
            "activist": form.startswith("SC 13D"),
            "amendment": form.endswith("/A"),
            "filed_date": f.get("filed_date", ""),
            "accession": f.get("accession", ""),
            # Who took the stake. It was in the header all along; without it
            # the list could only say that SOMEBODY filed.
            "filer": filer_name,
            "filer_cik": filer_cik,
            "url": acc_dir.format(int(cik), f["accession"].replace("-", ""),
                                  f.get("primary_doc", "")),
        })
    return {
        "symbol": str(symbol).upper(),
        "cik": cik,
        "company": company_name_for_symbol(symbol),
        "window_months": months,
        "since": since,
        "filings_found": total,
        "filings_truncated": truncated,
        # Schedules this CIK filed about other companies, and schedules whose
        # header could not be read — neither is a stake in this company.
        "filed_by_this_cik": filed_by_this_cik,
        "filings_unverified": unverified,
        "stakes": out,
    }


def handle_action(action, payload):
    symbol = payload.get("symbol") or ""
    months = int(payload.get("months") or 24)
    max_filings = int(payload.get("max_filings") or 80)
    if action in ("insiders", "stakes", "all") and not symbol and not payload.get("cik"):
        return {"error": "symbol or cik required"}

    cik = payload.get("cik")
    if action == "insiders":
        return fetch_insiders(symbol, months, max_filings, cik)
    if action == "stakes":
        return fetch_stakes(symbol, months, cik, max_filings)
    if action == "all":
        ins = fetch_insiders(symbol, months, max_filings, cik)
        stk = fetch_stakes(symbol, months, cik, max_filings)
        return {
            "symbol": str(symbol).upper(),
            "insiders": ins,
            "stakes": stk,
        }
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "all"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
