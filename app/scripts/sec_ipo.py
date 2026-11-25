"""What an IPO's own SEC filings say: the price range of a registration, the
date, price and lock-up term of a final prospectus.

The weekly brief is public, and Nasdaq's calendar may not be redistributed. It
is read to know WHICH companies to look at; everything the brief prints about
them comes from here. A filing that does not say it in the usual words gives None,
never a guess.
"""

import html
import re
from datetime import date, datetime, timedelta

import sec_http

ARCHIVE = "https://www.sec.gov/Archives/edgar/data/{}/{}/{}"
FINAL_FORMS = ("424B4", "424B1")
REG_FORMS = ("S-1", "S-1/A", "F-1", "F-1/A")
CACHE_KEY = "secipo4:{}"          # bump with the patterns below
CACHE_DAYS = 400 * 86400          # a filing does not change

_DATE = r"([A-Z][a-z]+ \d{1,2} ?, ?\d{4})"


def plain(markup):
    """A filing's text: tags out, entities read, white space single."""
    t = re.sub(r"(?is)<(script|style).*?</\1>", " ", markup or "")
    t = html.unescape(re.sub(r"<[^>]+>", " ", t))
    return re.sub(r"\s+", " ", t.replace("​", "").replace("\xa0", " "))


_MONEY = r"(?:US ?)?\$ ?\[? ?([\d][\d.,]*) ?\]?"


def price_range(text):
    """(low, high) of the estimated offering price, or None. A single price
    (a SPAC's $10.00) is (p, p). Only where the sentence is about the offering
    price: a filing has many ranges (revenue, conversion and exercise prices)."""
    num = lambda x: float(x.replace(",", "").rstrip("."))
    for m in re.finditer(r"(?:between|in the range of|range (?:is )?from) " + _MONEY + r" (?:and|to) " + _MONEY
                         + r"(.{0,12})", text):
        before = text[max(0, m.start() - 160):m.start()].lower()
        said = re.split(r"\. (?=[a-z])|; ", before)[-1]  # this sentence
        if "offering price" not in said or re.search(r"conver|exercis", said) or re.search(r"[mb]illion", m.group(3)):
            continue
        lo, hi = num(m.group(1)), num(m.group(2))
        return (lo, hi) if 0 < lo <= hi <= lo * 3 else None
    m = re.search(r"public offering price (?:of|is|will be|per (?:unit|share) (?:of|is)) " + _MONEY, text)
    if m:
        p = num(m.group(1))
        return (p, p) if p > 0 else None
    return None


def prospectus_date(text):
    for pattern in (r"the date of this prospectus is ", r"(?<!preliminary )prospectus,? dated "):
        m = re.search(pattern + _DATE, text, re.I)
        if m:
            try:
                return datetime.strptime(re.sub(r" ?, ?", ", ", m.group(1)), "%B %d, %Y").date()
            except ValueError:
                return None
    return None


def lockup_terms(text):
    """The terms, in days after the prospectus, that sentences about directors,
    officers or other holders agreeing not to sell state — usually one; the
    company's own standstill or a staged release can add others."""
    found = set()
    for m in re.finditer(r"(\d{2,3})(?:th)?[ -]days? .{0,20}?(?:after|following|from) the date of (?:this|the) "
                         r"(?:final )?prospectus", text):
        # An over-allotment option (30 days), a delivery rule (25) and Rule 144
        # (90 days for everybody) are written the same way.
        near = text[max(0, m.start() - 500):m.end() + 200].lower()
        n = int(m.group(1))
        if (60 <= n <= 365 and "rule 144" not in near and "rule 701" not in near
                and re.search(r"director|officer|holder|insider|founder", near)
                and re.search(r"lock-?up|not to (?:offer|sell|transfer|dispose)|not,? .{0,60}(?:offer|sell|dispose)", near)):
            found.add(n)
    return sorted(found)


def symbol(text):
    m = re.search(r"under the (?:\w+ )?symbols? [\"“‘']([A-Z][A-Z.]{0,5})[,.]?[\"”’']", text)
    return m.group(1).rstrip(".") if m else ""


def _text(cik, row, cache):
    """A filing's parsed facts, read once: {range, dated, lockup} as JSON."""
    key = CACHE_KEY.format(row["accession"])
    hit = cache.get(key, CACHE_DAYS)
    if hit is not None:
        return hit
    url = ARCHIVE.format(int(cik), row["accession"].replace("-", ""), row["doc"])
    r, _ = sec_http.get(url, timeout=40)
    if r is None:
        return None
    t = plain(r.text)
    d = prospectus_date(t)
    out = {"range": price_range(t[:60_000]), "dated": d.isoformat() if d else None, "lockup": lockup_terms(t),
           "symbol": symbol(t)}
    cache.put(key, out)
    return out


def _filings(cik, cache):
    """The company's filings, newest first, and its SEC name. (None, None)
    when SEC did not answer."""
    import ipo_calendar as ipo

    sub = ipo.submissions_many(cache, [(str(cik), False)]).get(str(cik))
    if not sub:
        return None, None
    rec = (sub.get("filings") or {}).get("recent") or {}
    rows = [dict(zip(("form", "date", "accession", "doc"), r)) for r in zip(
        rec.get("form") or [], rec.get("filingDate") or [], rec.get("accessionNumber") or [],
        rec.get("primaryDocument") or [])]
    return sorted(rows, key=lambda r: r["date"], reverse=True), sub.get("name")


def ahead(deals, lo, hi, cache):
    """IPOs expected to price between @lo and @hi: ([{company, ticker, range,
    step, filed}], how many could not be read). finterm's calendar says which
    (a date set, or a listing step on file); the rest is their SEC filings'."""
    out, unread = [], 0
    for d in deals:
        p = d.get("projection") or {}
        if p.get("kind") not in ("scheduled", "imminent") or not p.get("date"):
            continue
        first, last = p.get("lo") or p["date"], p.get("hi") or p["date"]
        if last < lo.isoformat() or first > hi.isoformat():
            continue
        rows, name = _filings(d["cik"], cache) if d.get("cik") else (None, None)
        regs = [r for r in rows or [] if r["form"] in REG_FORMS]
        if not regs or not name:
            unread += 1
            continue
        reg = regs[0]
        # An amendment can be exhibits only: the newest that states a price.
        facts = [_text(d["cik"], r, cache) or {} for r in regs[:3]]
        price = next((f["range"] for f in facts if f.get("range")), None)
        step = next((r for r in rows if r["form"] in ("EFFECT", "CERT", "8-A12B") and r["date"] >= reg["date"]), None)
        out.append({"company": name, "ticker": next((f["symbol"] for f in facts if f.get("symbol")), ""), "range": price,
                    "step": (step or reg)["form"], "filed": (step or reg)["date"], "spac": bool(d.get("spac"))})
    return sorted(out, key=lambda r: (r["spac"], r["company"])), unread


def lockups(rows, lo, hi, cache):
    """Lock-ups that end between @lo and @hi by the final prospectus: ([{ticker,
    company, dated, days, ends}], how many more could not be confirmed)."""
    out, unread = [], 0
    for r in rows:
        # The calendar's date can be off by the term it assumed: ten days round
        # the window are looked at, and the prospectus decides.
        if not (lo - timedelta(days=10)).isoformat() <= r["expiry"] <= (hi + timedelta(days=10)).isoformat():
            continue
        filings, name = _filings(r["cik"], cache) if r.get("cik") else (None, None)
        priced = date.fromisoformat(r["priced_date"])
        near = [f for f in filings or [] if f["form"] in FINAL_FORMS and abs((date.fromisoformat(f["date"]) - priced).days) <= 7]
        facts = (_text(r["cik"], near[-1], cache) if near else None) or {}
        dated = date.fromisoformat(facts["dated"]) if facts.get("dated") else None
        # A final prospectus is filed within days of its date; another date in
        # it (a preliminary one's) is not it. The term: the calendar's, when the
        # prospectus states it too; without one, the only one stated.
        terms = facts.get("lockup") or []
        days = (r["lockup_days"] if r.get("basis") == "term" and r["lockup_days"] in terms
                else terms[0] if r.get("basis") != "term" and len(terms) == 1 else None)
        if not (name and dated and days and 0 <= (date.fromisoformat(near[-1]["date"]) - dated).days <= 5):
            unread += lo.isoformat() <= r["expiry"] <= hi.isoformat()
            continue
        ends = dated + timedelta(days=days)
        if lo <= ends <= hi:
            out.append({"ticker": facts.get("symbol") or "", "company": name, "dated": dated, "days": days,
                        "ends": ends})
    return sorted(out, key=lambda r: (r["ends"], r["company"])), unread
