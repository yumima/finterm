"""The company's own words, for the ER Report tab.

    report_filings.py <TICKER>

From the latest annual report (10-K): Business, Risk Factors and Management's
Discussion; from the latest quarterly report (10-Q): Management's Discussion.
Each section is cleaned and cut at a paragraph boundary to a size a reader —
or a language model — can take in; `length` is the original size and
`truncated` says whether it was cut.

Also, from the annual report's XBRL: revenue by segment, product and
geography for the last two fiscal years, and the customers it names as a
disclosed share of revenue.

Prints {"symbol", "annual": {form, filing_date, period, accession, url,
sections}, "quarterly": {...}, "revenue_breakdown": [...], "customers": [...]}
— either filing may be null with `<name>_error` saying why (a foreign issuer
files a 20-F and no quarterly report) — or {"error": str} when neither could
be read.
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
    url = getattr(filing, "homepage_url", None) or getattr(filing, "filing_url", None)
    return {"form": form, "filing_date": str(filing.filing_date),
            "period": str(period) if period else None,
            "accession": str(filing.accession_number), "url": str(url) if url else None, "sections": {}}


# ── Revenue by segment, product and geography (XBRL) ────────────────────────
# A filing tags its revenue disaggregation with one "axis" per cut. The values
# are the company's own audited figures; nothing here estimates anything.

REVENUE_CONCEPTS = (
    "us-gaap:Revenues",
    "us-gaap:RevenueFromContractWithCustomerExcludingAssessedTax",
    "us-gaap:RevenueFromContractWithCustomerIncludingAssessedTax",
    "us-gaap:SalesRevenueNet",
    "ifrs-full:Revenue",
)
AXES = (
    ("segment", ("us-gaap_StatementBusinessSegmentsAxis", "ifrs-full_SegmentsAxis")),
    ("product", ("srt_ProductOrServiceAxis", "ifrs-full_ProductsAndServicesAxis")),
    ("geography", ("srt_StatementGeographicalAxis", "ifrs-full_GeographicalAreasAxis")),
)
RECONCILE_LO, RECONCILE_HI = 97.0, 103.0
MAX_ROWS = 8
_SALES_BENCHMARKS = ("SalesRevenueNetMember", "RevenueFromContractWithCustomerMember", "RevenuesMember",
                     "RevenueFromContractWithCustomerBenchmarkMember")


def humanize(member):
    """'nvda:DataCenterMember' -> 'Data Center' when the filing gives no label."""
    name = str(member).split(":")[-1]
    name = re.sub(r"Member$", "", name)
    name = re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", " ", name)
    return name.strip() or str(member)


def _is_sum_of_smaller(value, smaller, tolerance):
    n = len(smaller)
    for mask in range(1, 1 << n):
        if mask & (mask - 1) == 0:
            continue  # a single row equal to another is a duplicate tag, not a sum
        total = sum(v for k, v in enumerate(smaller) if mask >> k & 1)
        if abs(total - value) <= tolerance * value:
            return True
    return False


def drop_subtotals(rows, total=None):
    """Keep the leaves of a breakdown.

    A filing tags subtotals on the same axis as their parts (Data Center next
    to Compute and Networking; Total International next to its regions), and
    charting both counts the money twice. `rows` is a list of (label, value).

    With the year's `total` revenue known, rows are removed only while they
    add up to MORE than it — a breakdown that already sums to revenue has no
    subtotal in it, however many of its rows happen to equal a sum of others
    (with ten rows, some always do). The row removed is the largest that is
    the sum of two or more smaller ones. Without a total the match has to be
    near-exact to count."""
    rows = sorted([r for r in rows if r[1] is not None and r[1] > 0], key=lambda r: -r[1])
    if len(rows) > 14:  # the search is exponential; a breakdown this fine is not one to chart
        return rows
    tolerance = 0.004 if total else 0.0005
    while len(rows) > 2:
        if total and sum(v for _, v in rows) <= total * 1.01:
            break
        for i, (_, value) in enumerate(rows):
            if _is_sum_of_smaller(value, [v for _, v in rows[i + 1:]], tolerance):
                del rows[i]
                break
        else:
            break
    return rows


def _is_annual(start, end):
    """A period of roughly a year (52/53-week fiscal years included)."""
    try:
        from datetime import date as _d
        return 350 <= (_d.fromisoformat(str(end)[:10]) - _d.fromisoformat(str(start)[:10])).days <= 380
    except Exception:
        return False


def _annual_only(frame):
    """The frame's full-year facts. A filing reports the fourth quarter under
    the same period END as the year — only the duration tells them apart."""
    if frame.empty:
        return frame
    return frame[[_is_annual(a, b) for a, b in zip(frame["period_start"], frame["period_end"])]]


def _year_ends(frame):
    return sorted({str(e)[:10] for e in frame["period_end"]}, reverse=True)


# Axes that label a figure without dividing it: a segment's revenue is often
# tagged "operating segments" on the consolidation axis, a country's
# "geographic concentration". Any OTHER member on the consolidation axis
# (eliminations, corporate) is a different figure and is not taken.
_LABEL_ONLY = {
    "ConsolidationItemsAxis": ("OperatingSegmentsMember",),
    "ConcentrationRiskByTypeAxis": None,       # any member
    "ConcentrationRiskByBenchmarkAxis": _SALES_BENCHMARKS,
}


def _not_cut_further(frame, dims, axis_col):
    """Rows whose only real dimension is `axis_col`."""
    keep = [True] * len(frame)
    for c in dims:
        if c == axis_col:
            continue
        allowed = next((v for k, v in _LABEL_ONLY.items() if c.endswith(k)), ())
        vals = frame[c]
        for i, v in enumerate(vals):
            if v is None or v != v:
                continue
            if allowed == () or (allowed is not None and not str(v).endswith(tuple(allowed))):
                keep[i] = False
    return keep


def revenue_breakdown(df, label_of):
    """[{axis, period, prior_period, coverage_pct, rows: [{label, value, prior}]}]
    from a filing's dimensional facts (a DataFrame with dim_* columns).
    `coverage_pct` is the rows' share of the year's total revenue."""
    if df is None or getattr(df, "empty", True) or "concept" not in df.columns:
        return []
    dims = [c for c in df.columns if c.startswith("dim_")]
    rev = df[df["concept"].astype(str).isin(REVENUE_CONCEPTS)]
    if rev.empty:
        return []
    rev = _annual_only(rev)
    if rev.empty:
        return []
    plain = rev[rev[dims].isna().all(axis=1)] if dims else rev
    totals = {}
    for _, r in plain.iterrows():
        if r["numeric_value"] == r["numeric_value"]:
            end = str(r["period_end"])[:10]
            totals[end] = max(totals.get(end, 0.0), float(r["numeric_value"]))
    out = []
    for axis, names in AXES:
        col = next((f"dim_{n}" for n in names if f"dim_{n}" in rev.columns), None)
        if col is None:
            continue
        only = rev[rev[col].notna() & _not_cut_further(rev, dims, col)]
        ends = _year_ends(only)
        if not ends:
            continue
        by_year = {}
        for _, r in only.iterrows():
            end = str(r["period_end"])[:10]
            v = r["numeric_value"]
            if end in ends[:2] and v == v:
                by_year.setdefault(end, {})[str(r[col])] = float(v)
        latest = by_year.get(ends[0], {})
        leaves = drop_subtotals([(m, v) for m, v in latest.items()], totals.get(ends[0]))
        if len(leaves) < 2:
            continue
        prior = by_year.get(ends[1], {}) if len(ends) > 1 else {}
        rows = [{"label": label_of(m), "value": v, "prior": prior.get(m)} for m, v in leaves]
        total = totals.get(ends[0])
        coverage = sum(r["value"] for r in rows) / total * 100.0 if total else None
        # Published only when it reconciles to the year's revenue. An axis that
        # does not is carrying two different cuts at once (Microsoft tags
        # "Product / Service" and its product lines on the same axis), and a
        # chart of it would be of nothing in particular.
        if coverage is None or not (RECONCILE_LO <= coverage <= RECONCILE_HI):
            continue
        if len(rows) > MAX_ROWS:  # the tail as one row, so the bars still sum to revenue
            tail = rows[MAX_ROWS - 1:]
            priors = [r["prior"] for r in tail]
            rows = rows[:MAX_ROWS - 1] + [{
                "label": f"Other ({len(tail)})", "value": sum(r["value"] for r in tail),
                "prior": sum(priors) if all(p is not None for p in priors) else None}]
        out.append({"axis": axis, "period": ends[0], "prior_period": ends[1] if len(ends) > 1 else None,
                    "coverage_pct": coverage, "rows": rows})
    return out


def customer_concentration(df, label_of):
    """Customers the filing says account for a disclosed share of REVENUE in
    the latest year: [{label, pct, scope}]. Receivables concentration is left
    out — it is a credit fact, not a sales one."""
    if df is None or getattr(df, "empty", True):
        return []
    cust = next((c for c in df.columns if c.endswith("MajorCustomersAxis")), None)
    bench = next((c for c in df.columns if c.endswith("ConcentrationRiskByBenchmarkAxis")), None)
    if cust is None or bench is None:
        return []
    seg = next((c for c in df.columns if c.endswith("StatementBusinessSegmentsAxis")), None)
    kind = next((c for c in df.columns if c.endswith("ConcentrationRiskByTypeAxis")), None)
    # Cut any other way (a customer's share of one COUNTRY's sales, say) the
    # percentage is of something smaller than revenue, and is not listed.
    stray = [c for c in df.columns if c.startswith("dim_") and c not in (cust, bench, seg, kind)]
    rows = df[df["concept"].astype(str).str.contains("ConcentrationRiskPercentage") & df[cust].notna()
              & df[bench].astype(str).str.endswith(_SALES_BENCHMARKS)
              & (df[stray].isna().all(axis=1) if stray else True)]
    # The year's share, not the fourth quarter's: both carry the same period end.
    rows = _annual_only(rows)
    if rows.empty:
        return []
    latest = str(rows["period_end"].astype(str).max())[:10]
    out, seen = [], set()
    for _, r in rows[rows["period_end"].astype(str).str[:10] == latest].iterrows():
        v = r["numeric_value"]
        if v != v or v <= 0:
            continue
        scope = label_of(r[seg]) if seg and r[seg] == r[seg] and r[seg] is not None else ""
        key = (str(r[cust]), scope)
        if key in seen:
            continue
        seen.add(key)
        out.append({"label": label_of(r[cust]), "pct": round(float(v) * 100.0 if v <= 1.0 else float(v), 2),
                    "scope": scope, "period": latest})
    return sorted(out, key=lambda x: -x["pct"])[:8]


def _xbrl_parts(filing):
    """(breakdown, customers) from a filing's XBRL; ([], []) when it has none."""
    xb = filing.xbrl()
    if xb is None:
        return [], []
    df = xb.facts.query().with_dimensions().to_dataframe()
    catalog = getattr(xb, "element_catalog", None) or {}

    def label_of(member):
        el = catalog.get(str(member).replace(":", "_"))
        labels = getattr(el, "labels", None) or {}
        for role in ("terseLabel", "verboseLabel", "label"):
            for k, v in labels.items():
                if k.endswith("/" + role) and v:
                    text = re.sub(r"\s*\[Member\]$", "", str(v).replace("\xa0", " ")).strip()
                    return text.title() if text.isupper() and len(text) > 3 else text
        return humanize(member)

    return revenue_breakdown(df, label_of), customer_concentration(df, label_of)


def fetch(symbol):
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no ticker given"}
    try:
        from sec_edgar.base import Company, check_edgar_available, initialize_edgar
        check_edgar_available()
        initialize_edgar()  # declares the configured contact — SEC refuses requests without one
        company = Company(symbol)
    except Exception as e:
        return {"error": _explain(e)}

    out = {"symbol": symbol, "annual": None, "quarterly": None, "revenue_breakdown": [], "customers": []}
    annual_parts = (("business", "business"), ("risk_factors", "risk_factors"), ("mdna", "management_discussion"))
    for name, forms, parts in (
        # A foreign issuer files its annual report as a 20-F (Canada: 40-F)
        # and no quarterly report at all.
        ("annual", ("10-K", "20-F", "40-F"), annual_parts),
        ("quarterly", ("10-Q",), (("mdna", "part_i_item_2"),)),
    ):
        problem = None
        for form in forms:
            try:
                # amendments=False: a 10-K/A often carries only Part III, and as
                # the newest filing it would stand in for the report itself.
                filing = company.get_filings(form=form, amendments=False).latest(1)
                if not filing:
                    problem = problem or f"no {forms[0]} on file"
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
                if name == "annual":
                    # The figures do not depend on the prose having been split.
                    try:
                        out["revenue_breakdown"], out["customers"] = _xbrl_parts(filing)
                        out["breakdown_source"] = f"{form} filed {filing.filing_date}"
                    except Exception as e:
                        out["breakdown_error"] = str(e)[:160]
                if doc["sections"] or (name == "annual" and out["revenue_breakdown"]):
                    out[name] = doc
                    problem = None
                    break
                problem = f"the {form} could not be split into sections"
            except Exception as e:
                problem = _explain(e)
        if problem:
            out[f"{name}_error"] = problem
    if out["annual"] is None and out["quarterly"] is None:
        return {"error": out.get("annual_error") or out.get("quarterly_error") or "no filings read",
                "symbol": symbol}
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps({"error": "usage: report_filings.py <TICKER>"}))
        sys.exit(1)
    # The filing library reports its parser fallbacks on stdout. The caller
    # reads the first JSON value there, so everything but the answer goes to
    # stderr.
    import contextlib
    with contextlib.redirect_stdout(sys.stderr):
        result = fetch(sys.argv[1])
    print(json.dumps(result))
