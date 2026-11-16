"""The company's own words, for the ER Report tab.

    report_filings.py <TICKER>

From the latest annual report (10-K): Business, Risk Factors and Management's
Discussion; from the latest quarterly report (10-Q): Management's Discussion.
Each section is cleaned and cut at a paragraph boundary to a size a reader —
or a language model — can take in; `length` is the original size and
`truncated` says whether it was cut.

Prints {"symbol", "annual": {form, filing_date, period, accession, sections},
"quarterly": {...}} — either filing may be null with `<name>_error` saying why
(a foreign issuer files a 20-F, not a 10-K) — or {"error": str} when neither
could be read.
"""

import json
import re
import sys

LIMITS = {"business": 16000, "risk_factors": 12000, "mdna": 14000}

_PAGE_NO = re.compile(r"^\s*\d{1,3}\s*$")
_TOC = re.compile(r"^\s*table of contents\s*$", re.I)


def clean(text):
    """Drop page numbers and 'Table of Contents' furniture, normalise spaces,
    collapse blank runs."""
    if not text:
        return ""
    out, blank = [], False
    for line in str(text).replace("\xa0", " ").splitlines():
        line = re.sub(r"[ \t]+", " ", line).strip()
        if _PAGE_NO.match(line) or _TOC.match(line):
            continue
        if not line:
            if not blank and out:
                out.append("")
            blank = True
            continue
        blank = False
        out.append(line)
    return "\n".join(out).strip()


def cut(text, limit):
    """`text` up to `limit` characters, ending on a paragraph (else sentence)."""
    if len(text) <= limit:
        return text, False
    head = text[:limit]
    stop = head.rfind("\n\n")
    if stop < limit * 0.6:
        stop = max(head.rfind(". "), head.rfind(".\n")) + 1
    if stop < limit * 0.6:
        stop = limit
    return head[:stop].rstrip(), True


def risk_summary(text):
    """A 10-K's own 'Risk Factors Summary' when it has one — the company's
    list of what matters most — otherwise the section from its start."""
    m = re.search(r"risk factors? summary", text, re.I)
    if not m:
        return text
    rest = text[m.start():]
    # The full section restarts under a second "Risk Factors" heading.
    again = re.search(r"\n\s*risk factors\s*\n", rest[200:], re.I)
    return rest[: 200 + again.start()] if again else rest


# Shorter than this a "section" is a pointer ("incorporated by reference to
# pages 45-130 of the Annual Report"), not the section.
MIN_SECTION = 1500


def skip_boilerplate(text):
    """Management's Discussion opens with a forward-looking-statements notice;
    start at the Overview when one follows closely."""
    m = re.search(r"\n(overview|executive overview|executive summary)\s*\n", text[:9000], re.I)
    return text[m.start():].lstrip() if m else text


def section(text, key):
    text = clean(text)
    if len(text) < MIN_SECTION:
        return None
    if key == "risk_factors":
        text_for_cut = risk_summary(text)
    elif key == "mdna":
        text_for_cut = skip_boilerplate(text)
    else:
        text_for_cut = text
    body, truncated = cut(text_for_cut, LIMITS[key])
    return {"text": body, "length": len(text), "truncated": truncated or len(text_for_cut) < len(text)}


def _explain(e):
    """An EDGAR failure in words a reader can act on. SEC answers 403 to any
    client that does not name a contact, which is a setting, not an outage."""
    msg = str(e)
    if "403" in msg or "identity" in msg.lower() or "Undeclared" in msg:
        return ("SEC EDGAR refused the request: it requires a contact email — "
                "set one in Settings › Credentials › Contact email")
    return f"SEC EDGAR lookup failed ({msg[:160]})"


def _read(obj, name):
    """A filing section by attribute (10-K: .business) or, failing that, from
    its section map (10-Q: sections['part_i_item_2'])."""
    text = getattr(obj, name, None)
    if not text:
        sections = getattr(obj, "sections", None)
        if sections is not None and hasattr(sections, "get"):
            text = sections.get(name)
    if text is not None and not isinstance(text, str):
        reader = getattr(text, "text", None)  # a Section object: its text() is the prose
        text = reader() if callable(reader) else reader
    return str(text) if text else None


def _meta(filing, form):
    period = getattr(filing, "period_of_report", None)
    return {"form": form, "filing_date": str(filing.filing_date),
            "period": str(period) if period else None,
            "accession": str(filing.accession_number), "sections": {}}


def fetch(symbol):
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no ticker given"}
    try:
        from mcp.edgar.base import Company, check_edgar_available, initialize_edgar
        check_edgar_available()
        initialize_edgar()  # declares the configured contact — SEC refuses requests without one
        company = Company(symbol)
    except Exception as e:
        return {"error": _explain(e)}

    out = {"symbol": symbol, "annual": None, "quarterly": None}
    for name, form, parts in (
        ("annual", "10-K", (("business", "business"), ("risk_factors", "risk_factors"),
                            ("mdna", "management_discussion"))),
        ("quarterly", "10-Q", (("mdna", "part_i_item_2"),)),
    ):
        try:
            # amendments=False: a 10-K/A often carries only Part III, and as
            # the newest filing it would stand in for the report itself.
            filing = company.get_filings(form=form, amendments=False).latest(1)
            if not filing:
                out[f"{name}_error"] = f"no {form} on file"
                continue
            obj = filing.obj()
            doc = _meta(filing, form)
            for key, attr in parts:
                try:
                    sec = section(_read(obj, attr), key)
                except Exception:
                    sec = None
                if sec:
                    doc["sections"][key] = sec
            if doc["sections"]:
                out[name] = doc
            else:
                out[f"{name}_error"] = f"the {form} could not be split into sections"
        except Exception as e:
            out[f"{name}_error"] = _explain(e)
    if out["annual"] is None and out["quarterly"] is None:
        return {"error": out.get("annual_error") or out.get("quarterly_error") or "no filings read",
                "symbol": symbol}
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps({"error": "usage: report_filings.py <TICKER>"}))
        sys.exit(1)
    print(json.dumps(fetch(sys.argv[1])))
