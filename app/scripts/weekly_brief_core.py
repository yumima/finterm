"""The weekly insider-buying brief — data, page text and post texts.

Shared by the app (social_app.py, behind the Social screen)
and the owner's publishing tools (tools/weekly_brief.py, tools/announce.py).
The app shows and posts the PUBLISHED issue (issue.json beside the page), so
a post always matches the page it links to.
Public output is SEC EDGAR data only.
"""
import re
from datetime import date, timedelta

CONTACT_KEY = "FINTERM_CONTACT_EMAIL"

# finterm's own forward-return study (plans/research/form4_forward_returns_2026-09.md):
# filings 2025-07-07 → 2026-06-30, returns in excess of SPY from the filing
# date. Its "cluster" is NOT this list's filter (30-day trailing window, no
# dollar floor vs 7 days and $50K here), so the text says what was measured,
# and that a lone insider's buy did nearly as well. Quoted in full — including
# the three-month fade — every week, whatever recent rows did.
CLUSTER_HISTORY = (
    "Context, not a forecast. finterm's own study of SEC filings from July 2025 to June 2026 looked at "
    "1,404 open-market buys filed when a different insider of the same company had bought in the previous "
    "30 days — related to this list, but a looser test (no dollar minimum, a longer window). Measured from "
    "the filing date, those stocks beat the S&P 500 (SPY) by a median 1.8% over the next month, 59% of the "
    "time; buys by a lone insider did nearly as well (median 1.2%, 55%). Over three months the median was "
    "−0.2%, a coin flip. Hypothetical, before costs and taxes, not adjusted for company size; past patterns "
    "do not predict future returns."
)
METHOD_URL = "https://github.com/yumima/finterm/blob/main/plans/research/form4_forward_returns_2026-09.md"

DISCLAIMER = (
    "This brief is general information compiled mechanically from public SEC EDGAR filings (Form 4). "
    "It is not investment advice, a recommendation, or an offer to buy or sell any security, and it does "
    "not consider any reader's circumstances. finterm and its authors are not registered investment "
    "advisers or broker-dealers. Backtest figures are hypothetical and past results do not predict future "
    "returns. Data may contain errors; verify against the filings. finterm receives no compensation from "
    "any company listed. Source: U.S. SEC, EDGAR."
)


def contact_from_keyring():
    """The SEC contact from finterm's keyring entry, or None. Never printed."""
    import finterm_keyring

    try:
        return finterm_keyring.get(CONTACT_KEY, legacy_names=("FINCEPT_CONTACT_EMAIL",))
    except finterm_keyring.KeyringUnavailable:
        return None


def money(v):
    if v is None:
        return "—"
    for unit, div in (("B", 1e9), ("M", 1e6), ("K", 1e3)):
        if abs(v) >= div:
            return f"${v / div:,.1f}{unit}"
    return f"${v:,.0f}"


_MD_SPECIAL = re.compile(r"([\\`*_\[\]<>|#])")


def md_escape(s):
    """EDGAR text in a Markdown table cell: never markup. Liquid syntax is
    handled by the {% raw %} block the table sits in."""
    s = str(s or "").replace("\n", " ").strip()
    return _MD_SPECIAL.sub(r"\\\1", s)


WINDOW_DAYS = 7


def clusters_data(scan):
    """This week's cluster rows, coverage notes, and whether the data is complete.

    Complete means every past business day in the window was read to the end
    (holidays count: EDGAR has no index for them, and the scan records that).
    Today is not required — its index may not be published yet.
    """
    import sec_form4_market as f4

    notes, complete = [], True
    if scan:
        try:
            res = f4.scan(days=WINDOW_DAYS)
        except Exception as e:
            res = {"error": f"{type(e).__name__}: {e}"}
        if res.get("error"):
            complete = False
            notes.append(f"The SEC scan did not finish this week ({res['error']}); rows below may be incomplete.")

    read, expected, missing = f4.coverage(WINDOW_DAYS)
    if missing:
        complete = False
        notes.append(f"Filings from {read} of {expected} business days in the window were read "
                     f"(missing: {', '.join(missing)}); the list may be incomplete.")
    res = f4.recent(days=WINDOW_DAYS, min_insiders=2, min_value=50_000.0)
    return res["rows"], notes, complete


def clusters_section(rows, notes):
    out = ["## Insider buying clusters", ""]
    out.append(
        "Companies where **two or more insiders** reported buying shares **on the open market** in SEC Form 4 "
        "filings from the last 7 days (transaction code P). Grants, option exercises, purchases by 10% owners "
        "and pre-scheduled 10b5-1 plan trades are excluded; joint filers count once. "
        "**Sorted by total dollars reported.**"
    )
    out.append("")
    out.append(f"{CLUSTER_HISTORY} [Method]({METHOD_URL}).")
    out.append("")
    if not rows:
        out.append("_No company met these conditions in this week's filings._")
    else:
        out.append("| Symbol | Company | Insiders | Reported | Avg price | Trade dates | Roles |")
        out.append("|---|---|---:|---:|---:|---|---|")
        for r in rows[:25]:
            avg = f"${r['avg_price']:,.2f}" if r.get("avg_price") else "—"
            dates = r["first_trade"] if r["first_trade"] == r["last_trade"] else f"{r['first_trade']} – {r['last_trade']}"
            out.append(
                f"| {md_escape(r['symbol']) or '—'} | {md_escape(r['issuer'])} | {r['insiders']} | "
                f"{money(r['value'])} | {avg} | {dates} | {md_escape(', '.join(r['roles'][:3]))} |"
            )
        if len(rows) > 25:
            out += ["", f"_{len(rows) - 25} more companies met the conditions; the 25 largest by dollars are shown._"]
    for n in notes:
        out += ["", f"> {n}"]
    out.append("")
    return out



SITE_URL = "https://yumima.github.io/finterm"
ISSUE_JSON_URL = f"{SITE_URL}/issue.json"  # the published issue, as data (write_site)

# Per network: (character limit, what any link counts as; None = its real
# length). The one copy: the generated texts and the app's counters both use it.
NETWORK_LIMITS = {"x": (280, 23), "bluesky": (300, None), "mastodon": (500, 23)}
MAX_CASHTAGS = {"x": 1}  # X: "Posts are limited to a maximum of one cashtag"


def issue_key(d):
    """One issue per ISO week: a rerun in the same week replaces the issue
    instead of publishing (and announcing) a second one."""
    y, w, _ = d.isocalendar()
    return f"{y}-W{w:02d}"


def issue_day(d):
    """The date an issue is filed under: Monday of its ISO week, so a rerun
    later in the same week keeps the same page and the same announced URL."""
    return d - timedelta(days=d.weekday())


def issue_url(d):
    # Mirrors `permalink: /brief/:year-:month-:day/` in site/_config.yml.
    return f"{SITE_URL}/brief/{issue_day(d):%Y-%m-%d}/"


# X weighs characters (twitter-text v3): these ranges count 1, everything
# else 2 — "…" (U+2026) and CJK included. Links count url_cost regardless.
_X_LIGHT = ((0, 4351), (8192, 8205), (8208, 8223), (8242, 8247))


def x_weight(text):
    return sum(1 if any(a <= ord(c) <= b for a, b in _X_LIGHT) else 2 for c in text)


def fit_post(head, items, tail, short_tail, url, limit, url_cost, weigh=len):
    """The longest post within @limit: drop list items from the end, then fall
    back to the short tail. @url_cost is what the network charges for the
    link (X and Mastodon: 23 for any URL; Bluesky: its real length)."""
    def cost(t):
        return weigh(t) - weigh(url) + url_cost

    for t in (tail, short_tail):
        for k in range(len(items), -1, -1):
            body = head if k == 0 else f"{head} Largest by $ reported: {', '.join(items[:k])}."
            text = f"{body}\n\n{t}"
            if cost(text) <= limit:
                return text
    return f"{head[:limit - cost(short_tail) - 3].rstrip()}…\n\n{short_tail}"


def _limit(network, url):
    limit, url_cost = NETWORK_LIMITS[network]
    return limit, len(url) if url_cost is None else url_cost


def social_posts(rows, today, url):
    """This issue's announcement per network, built from its own names and
    figures (X treats repeated identical posts as spam), in the page's neutral
    wording."""
    span = f"7 days to {today:%b %-d}"
    n = len(rows)
    if n:
        head = (f"Insider buying, {span} (SEC Form 4): {n} {'company' if n == 1 else 'companies'} "
                f"with 2+ insiders buying on the open market.")
    else:
        head = f"Insider buying, {span} (SEC Form 4): no company had 2+ insiders buying on the open market."
    def items_for(max_cashtags):
        out = []
        for r in rows:
            if len(out) == 3:
                break
            sym = r.get("symbol") or ""
            if sym:
                tag = f"${sym}" if len(out) < max_cashtags else sym
            else:
                tag = (r.get("issuer") or "")[:24].rstrip(" ,.")
            if not tag:
                continue  # a row with no name says nothing in a post
            out.append(f"{tag} {money(r['value'])} ({r['insiders']} insiders)")
        return out

    # X refuses a post with more than one cashtag (HTTP 403); the others take any.
    items = items_for(3)
    x_items = items_for(MAX_CASHTAGS["x"])
    tail = f"Full list, method and base rate: {url}"
    short = f"Full list: {url}"
    masto_tail = f"{tail}\n\nNot investment advice. #investing #stocks #insidertrading #opensource"
    return {
        "url": url,
        "x": fit_post(head, x_items, tail, short, url, *_limit("x", url), weigh=x_weight),
        "bluesky": fit_post(head, items, tail, short, url, *_limit("bluesky", url)),
        "mastodon": fit_post(head, items, masto_tail, short, url, *_limit("mastodon", url)),
    }


def page(front, body):
    """A Markdown page: YAML front matter (values JSON-quoted, which YAML
    reads as strings), the body, and the disclaimer."""
    import json

    fm = ["---"] + [f"{k}: {json.dumps(v, ensure_ascii=False) if isinstance(v, str) else v}"
                    for k, v in front.items()] + ["---", ""]
    return "\n".join(fm + body + [f"_{DISCLAIMER}_", ""])
