#!/usr/bin/env python3
"""
13F manager books — what the large discretionary shops own, and what changed.

Answers two questions off one index:

    BY FIRM    given a manager, their whole disclosed equity book with position
               weights and the quarter-over-quarter moves.
    BY STOCK   given a ticker, which tracked managers hold it, at what weight in
               THEIR book, and what they did last quarter.

The second is the one a flat holder table cannot answer. "BlackRock owns 8% of
AAPL" is mostly a statement that AAPL is in the index; "AAPL is 12% of Baupost's
book" is a statement that somebody with discretion decided something.

WHAT 13F IS NOT
    Long-only US equities. No shorts, no bonds, no cash, no leverage, and only
    some derivatives. So a weight here is a share of the manager's DISCLOSED
    EQUITY BOOK, never of their fund. For a concentrated long-only shop that is
    a conviction number; for a hedged multi-strategy book it is one leg of a
    position and reads as conviction only if you let it. The manager list
    carries a `style` for exactly this reason and the UI is expected to show it.

    It is also stale by construction: filed 45 days after quarter end, so a
    position opened early in the quarter is up to 135 days old when you see it.

THREE THINGS THE FILING FORMAT WILL DO TO YOU
    1. The information table is a SEPARATE document from primary_doc.xml, and
       its filename is arbitrary ("56757.xml"). It has to be found via the
       accession's index.json — it cannot be derived.
    2. The XML is namespaced. Tag lookups must ignore the namespace or match
       nothing at all, silently.
    3. A manager reports the SAME CUSIP on multiple rows — one per internal
       manager or discretion type. Berkshire files Ally Financial three times.
       Taking the first row understates the position and every weight computed
       from it. Rows must be aggregated by CUSIP.

ACTIONS
    book       {"cik", "quarters"?}          one manager's book + Q/Q diff
    holders    {"ticker", "ciks": [...]}     which of those managers hold it
    resolve    {"name"}                      manager name -> CIK
"""

import json
import re
import sys
import time
from collections import defaultdict
from xml.etree import ElementTree

try:
    import requests
except ImportError:
    requests = None

UA = {
    "User-Agent": "FinceptTerminal research@hanlexon.com",
    "Accept-Encoding": "gzip, deflate",
}

_LAST_REQ = 0.0
_MIN_REQ_GAP = 0.4

# SEC moved 13F value reporting from THOUSANDS to whole dollars for filings from
# 2023 onward. Getting this wrong is a silent 1000x error in every weight, so
# the date rule is backed by an empirical check below rather than trusted alone.
WHOLE_DOLLAR_FROM = "2023-01-01"


def _get(url, timeout=25):
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < _MIN_REQ_GAP:
        time.sleep(_MIN_REQ_GAP - elapsed)
    try:
        r = requests.get(url, timeout=timeout, headers=UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        _LAST_REQ = time.time()
        return None


def _pad_cik(cik):
    return str(cik).lstrip("0").zfill(10)


def _local(tag):
    """Tag name without its namespace. The information table declares a default
    namespace, so every findall() that spells a bare tag name matches nothing."""
    return tag.rsplit("}", 1)[-1]


# ── Curated managers ────────────────────────────────────────────────────────
# Discretionary shops where a position weight reads as a decision. Deliberately
# NOT "top N by 13F AUM": that list fills with multi-strategy and index books
# where the weight is one leg of a hedge or a benchmark artifact, and showing it
# as conviction would be the screen inventing confidence.
#
# `style` travels with each entry so the UI can say, once, what kind of book the
# reader is looking at. CIKs are resolved from EDGAR on first use rather than
# hardcoded here — a wrong CIK silently shows someone else's portfolio.
DEFAULT_MANAGERS = [
    {"name": "Berkshire Hathaway Inc", "cik": "0001067983", "style": "concentrated long"},
    {"name": "Baupost Group LLC", "style": "concentrated long"},
    {"name": "Pershing Square Capital Management", "style": "activist"},
    {"name": "Third Point LLC", "style": "activist"},
    {"name": "Appaloosa LP", "style": "concentrated long"},
    {"name": "Greenlight Capital", "style": "long/short"},
    {"name": "Lone Pine Capital", "style": "long/short"},
    {"name": "Tiger Global Management", "style": "long/short"},
    {"name": "Coatue Management", "style": "long/short"},
    {"name": "Viking Global Investors", "style": "long/short"},
    {"name": "Akre Capital Management", "style": "concentrated long"},
    {"name": "Fundsmith LLP", "style": "concentrated long"},
    {"name": "Polen Capital Management", "style": "concentrated long"},
    {"name": "Ruane Cunniff", "style": "concentrated long"},
    {"name": "Markel Group", "style": "concentrated long"},
    {"name": "Elliott Investment Management", "style": "activist"},
    {"name": "ValueAct Holdings", "style": "activist"},
    {"name": "Starboard Value LP", "style": "activist"},
    {"name": "Trian Fund Management", "style": "activist"},
    {"name": "Scion Asset Management", "style": "concentrated long"},
    # Index complexes, for context rather than conviction. A weight here says
    # nothing about a view; it is included so the reader can see how much of a
    # register is index money.
    {"name": "BlackRock Inc.", "style": "index complex"},
    {"name": "Vanguard Group Inc", "style": "index complex"},
    {"name": "State Street Corp", "style": "index complex"},
]


def resolve_manager_cik(name):
    """Manager name -> CIK, via EDGAR's company search.

    Resolved rather than hardcoded: a mistyped CIK does not fail loudly, it
    quietly shows a different firm's portfolio under the right name.
    """
    url = ("https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany"
           "&company=" + requests.utils.quote(name) +
           "&type=13F-HR&dateb=&owner=include&count=10&output=atom")
    r = _get(url)
    if r is None:
        return None
    # The atom feed carries the CIK either in a <CIK> element (single hit) or in
    # the entry links (multiple hits).
    m = re.search(r"<CIK>(\d{10})</CIK>", r.text)
    if m:
        return m.group(1)
    m = re.search(r"CIK=(\d{10})", r.text)
    return m.group(1) if m else None


# ── Filings ─────────────────────────────────────────────────────────────────

def recent_13f(cik, limit=4):
    """The manager's most recent 13F-HR filings, newest first."""
    r = _get(f"https://data.sec.gov/submissions/CIK{_pad_cik(cik)}.json")
    if r is None:
        return []
    try:
        subs = r.json()
    except Exception:
        return []
    rec = (subs.get("filings") or {}).get("recent") or {}
    acc = rec.get("accessionNumber") or []
    frm = rec.get("form") or []
    fdt = rec.get("filingDate") or []
    rdt = rec.get("reportDate") or []
    out = []
    for i in range(len(acc)):
        form = (frm[i] if i < len(frm) else "").upper()
        # 13F-HR/A amendments restate a quarter. Keeping only the original keeps
        # one row per period; an amendment would otherwise double the book.
        if form != "13F-HR":
            continue
        out.append({
            "accession": acc[i],
            "filed_date": fdt[i] if i < len(fdt) else "",
            "period": rdt[i] if i < len(rdt) else "",
        })
        if len(out) >= limit:
            break
    return out


def information_table_url(cik, accession):
    """Locate the information table document inside an accession.

    Its filename is assigned by the filing agent ("56757.xml"), so the directory
    listing is the only way to find it. primary_doc.xml is the cover page and
    contains no positions.
    """
    acc = accession.replace("-", "")
    r = _get(f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc}/index.json")
    if r is None:
        return None
    try:
        items = r.json()["directory"]["item"]
    except Exception:
        return None
    base = f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc}/"
    for it in items:
        n = (it.get("name") or "").lower()
        if n.endswith(".xml") and "primary_doc" not in n:
            return base + it["name"]
    return None


def parse_information_table(xml_bytes):
    """Positions from one information table, aggregated by CUSIP.

    Aggregation is not an optimisation. A manager files one row per internal
    manager or discretion type for the same security — Berkshire reports Ally
    Financial three times — so a per-row read understates the position and
    every weight derived from it.
    """
    try:
        root = ElementTree.fromstring(xml_bytes)
    except ElementTree.ParseError:
        return []

    agg = {}
    for node in root:
        if _local(node.tag) != "infoTable":
            continue
        row = {}
        for child in node:
            tag = _local(child.tag)
            if tag == "shrsOrPrnAmt":
                for g in child:
                    row[_local(g.tag)] = (g.text or "").strip()
            else:
                row[tag] = (child.text or "").strip()

        cusip = (row.get("cusip") or "").upper()
        if not cusip:
            continue
        # Only share positions. A principal amount is a bond line and cannot be
        # added to a share count.
        if (row.get("sshPrnamtType") or "SH").upper() != "SH":
            continue
        try:
            value = float(row.get("value") or 0)
            shares = float(row.get("sshPrnamt") or 0)
        except ValueError:
            continue

        if cusip not in agg:
            agg[cusip] = {
                "cusip": cusip,
                "issuer": row.get("nameOfIssuer") or "",
                "class": row.get("titleOfClass") or "",
                "value": 0.0,
                "shares": 0.0,
            }
        agg[cusip]["value"] += value
        agg[cusip]["shares"] += shares
    return list(agg.values())


def _normalise_values(positions, period):
    """Put values in whole dollars regardless of which reporting regime applied.

    SEC moved 13F from thousands to whole dollars for 2023 onward. The period
    date decides, but it is checked against the implied price per share, because
    a units error is invisible in the output and multiplies every weight by
    1000. Real equities do not trade below $1 in aggregate across a book; if the
    median implied price says otherwise, the values are in thousands whatever
    the date claims.
    """
    if not positions:
        return positions, "none"
    implied = sorted(p["value"] / p["shares"] for p in positions if p["shares"] > 0)
    median = implied[len(implied) // 2] if implied else 0.0

    by_date = period >= WHOLE_DOLLAR_FROM
    by_price = median >= 1.0
    scale = 1.0 if by_price else 1000.0
    basis = "whole dollars" if by_price else "thousands (scaled)"
    if by_date != by_price:
        basis += " — period date and implied price disagree; implied price used"
    for p in positions:
        p["value"] *= scale
    return positions, basis


def fetch_book(cik, quarters=2):
    """A manager's book for the last `quarters` filed periods, newest first."""
    if requests is None:
        return {"error": "requests not available"}
    filings = recent_13f(cik, limit=max(1, int(quarters)))
    if not filings:
        return {"error": f"no 13F-HR filings for CIK {cik}", "cik": _pad_cik(cik)}

    books = []
    for f in filings:
        url = information_table_url(cik, f["accession"])
        if not url:
            continue
        r = _get(url)
        if r is None:
            continue
        positions = parse_information_table(r.content)
        positions, basis = _normalise_values(positions, f.get("period", ""))
        total = sum(p["value"] for p in positions)
        for p in positions:
            p["weight"] = (p["value"] / total) if total > 0 else None
        positions.sort(key=lambda p: p["value"], reverse=True)
        books.append({
            "period": f.get("period", ""),
            "filed_date": f.get("filed_date", ""),
            "total_value": total,
            "position_count": len(positions),
            "value_basis": basis,
            "positions": positions,
        })

    result = {"cik": _pad_cik(cik), "books": books}
    if len(books) >= 2:
        result["moves"] = diff_books(books[0], books[1])
    return result


def diff_books(current, prior):
    """Quarter-over-quarter moves: new, exited, added, trimmed.

    Reported in shares rather than value so a move is not confused with a price
    change — a position can gain 20% of value while the manager sold into it.
    """
    cur = {p["cusip"]: p for p in current.get("positions", [])}
    old = {p["cusip"]: p for p in prior.get("positions", [])}
    moves = []

    # Share classes are separate securities with separate CUSIPs but the SAME
    # issuer name — Berkshire holds both Alphabet A and Alphabet C, and a moves
    # list keyed on the name alone shows "ALPHABET INC" twice with different
    # numbers and looks like a duplication bug. The class travels with the row.
    def label(p):
        cls = (p.get("class") or "").strip()
        return f"{p['issuer']} {cls}".strip() if cls and cls.upper() != "COM" else p["issuer"]

    for cusip, p in cur.items():
        before = old.get(cusip)
        if before is None:
            moves.append({"action": "new", "cusip": cusip, "issuer": p["issuer"],
                          "label": label(p), "class": p.get("class", ""),
                          "shares": p["shares"], "shares_delta": p["shares"],
                          "weight": p.get("weight")})
            continue
        delta = p["shares"] - before["shares"]
        if before["shares"] > 0 and abs(delta) / before["shares"] >= 0.01:
            moves.append({
                "action": "added" if delta > 0 else "trimmed",
                "cusip": cusip, "issuer": p["issuer"],
                "label": label(p), "class": p.get("class", ""),
                "shares": p["shares"], "shares_delta": delta,
                "pct_change": delta / before["shares"],
                "weight": p.get("weight"),
            })
    for cusip, p in old.items():
        if cusip not in cur:
            moves.append({"action": "exited", "cusip": cusip, "issuer": p["issuer"],
                          "label": label(p), "class": p.get("class", ""),
                          "shares": 0.0, "shares_delta": -p["shares"], "weight": 0.0})
    moves.sort(key=lambda m: abs(m.get("shares_delta") or 0), reverse=True)
    return {"from_period": prior.get("period", ""), "to_period": current.get("period", ""),
            "moves": moves}


# ── Ticker -> position lookup ───────────────────────────────────────────────

_SUFFIXES = re.compile(
    r"\b(inc|incorporated|corp|corporation|co|company|ltd|limited|plc|lp|llc|holdings?|"
    r"group|the|class|cl|com|new|sa|nv|ag)\b", re.I)


# Shortest normalised name that may be matched by prefix rather than exactly.
#
# Substring matching is what makes this dangerous. "Apple Inc." normalises to
# APPLE, and APPLE is a substring of APPLE HOSPITALITY REIT — so a naive
# contains() test reports a REIT position as an AAPL position, with a real share
# count and a real weight, presented as fact. That is worse than finding
# nothing: a missing row is visibly missing, a wrong row is not.
#
# Prefix matching is still needed because EDGAR abbreviates the core of a name
# (BANK OF AMER for BANK OF AMERICA), but those abbreviations are long. Ten
# characters admits them and excludes the short generic prefixes that collide.
MIN_PREFIX_MATCH = 10


def issuer_matches(target, candidate):
    """Is `candidate` the same issuer as `target`? Both already normalised.

    Deliberately biased towards saying no. A false negative leaves a manager off
    the list, which is visible and merely incomplete; a false positive attributes
    another company's position to this ticker.
    """
    if not target or not candidate:
        return False
    if target == candidate:
        return True
    shorter, longer = (target, candidate) if len(target) <= len(candidate) else (candidate, target)
    return len(shorter) >= MIN_PREFIX_MATCH and longer.startswith(shorter)


def _norm_issuer(name):
    """Issuer name reduced to a comparable core.

    13F reports CUSIPs, and the CUSIP-to-ticker map is licensed data that SEC
    does not publish — so the first match for a ticker has to be made on the
    issuer NAME. Once matched, the CUSIP is returned and callers can key on it
    exactly from then on.
    """
    n = (name or "").upper()
    n = re.sub(r"[^A-Z0-9 ]", " ", n)
    n = _SUFFIXES.sub(" ", n)
    return " ".join(n.split())


def find_holders(ticker, company_name, manager_ciks, quarters=2):
    """Which of `manager_ciks` hold `ticker`, and what they did last quarter."""
    target = _norm_issuer(company_name or ticker)
    if not target:
        return {"error": "no company name to match on", "ticker": ticker}

    holders, unmatched = [], []
    for cik in manager_ciks:
        book = fetch_book(cik, quarters=quarters)
        books = book.get("books") or []
        if not books:
            unmatched.append(_pad_cik(cik))
            continue
        current = books[0]
        hit = None
        for p in current.get("positions", []):
            if issuer_matches(target, _norm_issuer(p.get("issuer"))):
                hit = p
                break
        if not hit:
            continue

        entry = {
            "cik": _pad_cik(cik),
            "period": current.get("period", ""),
            "filed_date": current.get("filed_date", ""),
            "issuer": hit["issuer"],
            "cusip": hit["cusip"],
            "shares": hit["shares"],
            "value": hit["value"],
            "weight": hit.get("weight"),
            "book_total": current.get("total_value"),
            "position_count": current.get("position_count"),
        }
        for m in (book.get("moves") or {}).get("moves", []):
            if m["cusip"] == hit["cusip"]:
                entry["action"] = m["action"]
                entry["shares_delta"] = m["shares_delta"]
                entry["pct_change"] = m.get("pct_change")
                break
        else:
            # Present in both quarters with no material change is itself an
            # answer, and a different one from "we could not tell".
            entry["action"] = "held" if len(books) >= 2 else "unknown"
        holders.append(entry)

    holders.sort(key=lambda h: h.get("weight") or 0, reverse=True)
    return {"ticker": str(ticker).upper(), "matched_on": target,
            "holders": holders, "managers_without_filings": unmatched}


def handle_action(action, payload):
    if action == "managers":
        # resolve=true fills in the CIKs. Costs one EDGAR company-search per
        # manager, so it is opt-in: the caller does it once and stores the
        # result as the user's editable list.
        if payload.get("resolve"):
            out = []
            for m in DEFAULT_MANAGERS:
                entry = dict(m)
                entry["cik"] = m.get("cik") or (resolve_manager_cik(m["name"]) or "")
                out.append(entry)
            return {"managers": out}
        return {"managers": DEFAULT_MANAGERS}
    if action == "resolve":
        name = payload.get("name") or ""
        if not name:
            return {"error": "name required"}
        cik = resolve_manager_cik(name)
        return {"name": name, "cik": cik} if cik else {"name": name, "error": "not found"}
    if action == "book":
        cik = payload.get("cik")
        if not cik:
            return {"error": "cik required"}
        return fetch_book(cik, int(payload.get("quarters") or 2))
    if action == "holders":
        ciks = payload.get("ciks") or []
        resolved = []
        if not ciks:
            # No caller-supplied list means "use the curated defaults". Most of
            # those ship without a CIK on purpose — a hardcoded wrong CIK does
            # not fail loudly, it quietly shows a different firm's portfolio
            # under the right name — so they are resolved here and returned, so
            # the caller can persist them and never pay this again.
            for m in DEFAULT_MANAGERS:
                cik = m.get("cik") or resolve_manager_cik(m["name"])
                entry = dict(m)
                entry["cik"] = cik or ""
                resolved.append(entry)
                if cik:
                    ciks.append(cik)
        out = find_holders(payload.get("ticker") or "",
                           payload.get("company") or payload.get("ticker") or "",
                           ciks,
                           int(payload.get("quarters") or 2))
        if resolved:
            out["resolved_managers"] = resolved
        return out
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "managers"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
