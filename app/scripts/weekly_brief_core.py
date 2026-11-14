"""The weekly insider-buying brief — data, page text and post texts.

Shared by the app (social_app.py, behind the Social screen)
and the owner's publishing tools (tools/weekly_brief.py, tools/announce.py).
The app shows and posts the PUBLISHED issue (issue.json beside the page), so
a post always matches the page it links to.
Public output: SEC EDGAR data, the week's release schedule (no vendor
figures) and screen tickers with finterm's own backtest records (no vendor
figures) — see tools/weekly_brief.py.
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


# The weekly issue also carries the release schedule and the screens; it says
# where each came from (the insider-only page keeps DISCLAIMER).
ISSUE_DISCLAIMER = DISCLAIMER.replace(
    "compiled mechanically from public SEC EDGAR filings (Form 4).",
    "compiled mechanically from public SEC EDGAR filings (Form 4), a public economic-release calendar, and "
    "published stock-screening rules applied by finterm.").replace(
    "Source: U.S. SEC, EDGAR.",
    "Sources: insider filings, U.S. SEC, EDGAR; release dates as set by U.S. agencies, via the ForexFactory "
    "calendar; screen rankings computed by finterm from TradingView and Yahoo Finance market data, none of "
    "which is reproduced here.")
assert ISSUE_DISCLAIMER != DISCLAIMER


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


# An issue covers the trading week it follows. The run is on Sunday, and a
# run the timer catches up on Monday–Wednesday (machine off on Sunday) still
# belongs to the week just ended — keyed by its own ISO week it would take
# the NEXT week's key, and that Sunday's issue would replace it unannounced
# (one post per key). Thursday on files under the coming week.
ISSUE_LAG_DAYS = 3


def issue_key(d):
    """One issue per trading week: a rerun in the same week replaces the issue
    instead of publishing (and announcing) a second one."""
    y, w, _ = (d - timedelta(days=ISSUE_LAG_DAYS)).isocalendar()
    return f"{y}-W{w:02d}"


def issue_day(d):
    """The date an issue is filed under: Monday of its week (issue_key's), so
    a rerun later in the same week keeps the same page and announced URL."""
    a = d - timedelta(days=ISSUE_LAG_DAYS)
    return a - timedelta(days=a.weekday())


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


def page(front, body, disclaimer=None):
    """A Markdown page: YAML front matter (values JSON-quoted, which YAML
    reads as strings), the body, and the disclaimer."""
    import json

    fm = ["---"] + [f"{k}: {json.dumps(v, ensure_ascii=False) if isinstance(v, str) else v}"
                    for k, v in front.items()] + ["---", ""]
    return "\n".join(fm + body + [f"_{disclaimer or DISCLAIMER}_", ""])


# ── the week ahead: top-tier US macro releases ───────────────────────────────
#
# The schedule only: day, time (ET) and the release's name. Release dates are
# set and published by the agencies (BLS, BEA, Census, the Fed); the consensus
# "forecast" in the feed is the vendor's and is never written out.

FF_FEEDS = ("https://nfs.faireconomy.media/ff_calendar_thisweek.json",
            "https://nfs.faireconomy.media/ff_calendar_nextweek.json")


def macro_kind(title):
    """US release kind, for the top-tier rule. Mirrors kind_of() in
    app/src/services/calendar/MacroCalendar.h — keep the two in step."""
    t = (title or "").lower()
    if any(k in t for k in ("fomc", "federal funds", "fed chair", "powell", "fed ", "monetary policy")):
        return "FED"
    if any(k in t for k in ("non-farm", "nonfarm", "unemployment", "jobless", "payroll", "jolts",
                            "average hourly", "employment")):
        return "LABOR"
    if any(k in t for k in ("cpi", "ppi", "pce", "inflation")):
        return "INFLATION"
    if any(k in t for k in ("gdp", "retail sales", "ism ", "pmi")):
        return "GROWTH"
    return ""


def _impact(label):
    s = (label or "").strip().lower()
    if s.startswith("hol"):
        return 0
    return {"h": 3, "m": 2, "l": 1}.get(s[:1], 0)


def is_top_tier_us(row):
    """The WHAT'S ON panel's default filter (is_top_tier_us in
    MacroCalendar.h): US high impact, plus the medium-impact releases of a
    known kind (Fed speakers, jobless claims, PPI, …)."""
    if (row.get("country") or "").strip().upper() not in ("USD", "US"):
        return False
    imp = _impact(row.get("impact"))
    return imp >= 3 or (imp == 2 and bool(macro_kind(row.get("title"))))


def week_ahead(today):
    """The trading days ahead: on a weekend (the scheduled run) next Monday to
    Friday; on a weekday (a catch-up or manual rerun) today to Friday — never
    a week that skips this week's remaining releases."""
    if today.weekday() < 5:
        return today, today + timedelta(days=4 - today.weekday())
    mon = today + timedelta(days=7 - today.weekday())
    return mon, mon + timedelta(days=4)


def macro_events(rows, mon, fri):
    """Top-tier US releases between mon and fri (US Eastern dates), one item
    per instant — "CPI m/m", "Core CPI m/m" and "CPI y/y" at 8:30 are one
    release. Sorted by time; a row the feed repeats (both weekly files) once."""
    by_when = {}
    for r in rows:
        if not is_top_tier_us(r):
            continue
        when = _et_instant(r)
        if when is None:
            continue
        if not (mon <= when.date() <= fri):
            continue
        title = " ".join(str(r.get("title") or "").split())
        if not title:
            continue
        item = by_when.setdefault(when, {"when": when, "titles": []})
        if title not in item["titles"]:
            item["titles"].append(title)
    return [by_when[k] for k in sorted(by_when)]


def _et_instant(row):
    """The row's instant in US Eastern time (its date is the ET date), or None."""
    from datetime import datetime
    from zoneinfo import ZoneInfo

    try:
        return datetime.fromisoformat(str(row.get("date") or "").strip()).astimezone(ZoneInfo("America/New_York"))
    except (ValueError, TypeError):
        return None


def fetch_macro_rows(timeout=20, retry_wait=300):
    """Both weekly files (Sunday–Saturday weeks) from the calendar feed.
    Returns (rows, failures). Next week's file is published only part of the
    week — on a Saturday it is a 404 — so a 404 is "not published", not a
    failure; whether the rows cover the week ahead is macro_coverage()'s call."""
    import json
    import urllib.error
    import urllib.request

    import time

    def get(url):
        req = urllib.request.Request(url, headers={"User-Agent": "finterm-weekly-brief"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        if not isinstance(data, list):
            raise ValueError("not a list")
        return [r for r in data if isinstance(r, dict)]

    rows, failed = [], []
    for url in FF_FEEDS:
        name = url.rsplit("/", 1)[-1]
        for attempt in (1, 2):
            try:
                rows += get(url)
                break
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    break
                # The feed rate-limits (429): one patient retry, not a lost week.
                if e.code == 429 and attempt == 1 and retry_wait:
                    time.sleep(retry_wait)
                    continue
                failed.append(f"{name}: HTTP {e.code}")
                break
            except Exception as e:  # noqa: BLE001 — any failure is reported, not hidden
                failed.append(f"{name}: {type(e).__name__}")
                break
    return rows, failed


def macro_coverage(rows, mon, fri, failed=()):
    """None when the rows hold the week ahead, else why not. The feed lists
    every country every weekday, so a week with no row at all on its days is
    a week whose file was not read — never a quiet week."""
    for r in rows:
        w = _et_instant(r)  # the same day macro_events puts it on
        if w is not None and mon <= w.date() <= fri:
            return None
    if failed:
        return f"the release calendar could not be read ({'; '.join(failed)})"
    return "the calendar for that week was not published yet when this issue was built"


def macro_section(events, mon, fri, error=None):
    out = [f"## The week ahead — US releases, {mon:%b %-d} to {fri:%b %-d}", ""]
    out.append(
        "The scheduled US releases that move the whole market: everything rated high impact, plus Fed "
        "speakers, jobless claims and the other inflation, labor and growth reports. Times are US Eastern. "
        "Dates are set by the agencies and can move."
    )
    out.append("")
    if error:
        out.append(f"_No release list this week: {md_escape(error)}._")
    elif not events:
        out.append("_No top-tier US release is scheduled in the window._")
    else:
        out.append("| Day | Time (ET) | Release |")
        out.append("|---|---|---|")
        for ev in events:
            w = ev["when"]
            t = "—" if (w.hour, w.minute) == (0, 0) else w.strftime("%-I:%M %p")
            names = " · ".join(md_escape(x) for x in ev["titles"])
            out.append(f"| {w:%a %b %-d} | {t} | {names} |")
    out.append("")
    return out


# ── the top name from each strategy screen ───────────────────────────────────
#
# Screen output, not a recommendation. Published: the ticker, the company's
# name, the screen's rule in words and finterm's OWN backtest record. The
# metrics behind the ranking come from TradingView and Yahoo, whose terms do
# not allow redistribution, so no price, ratio or return of theirs is written.

# RS Leaders is the Trend Template's top 25 by RS: both lists start with the
# same name every week, so the template is represented by RS Leaders.
PICK_SKIP = ("trend_template",)

# The app's summary in the imperative ("buy the break of …") reads, beside a
# ticker on a public page, as an instruction about that stock. Described
# instead, in the third person, like every other screen's.
PUBLIC_SUMMARY = {
    "luk_pullback": "Market leaders resting at their rising 21-day average; the method enters on a break of "
                    "the prior day's high with a 1-3% stop.",
}


def screen_picks(doc):
    """[(strategy, symbol, company, rank)] — the first-ranked name of each
    screen; a name already listed under an earlier screen passes to the next
    one down (rank says which). A screen with no passer has symbol None."""
    rows = doc.get("rows") or {}
    taken, out = set(), []
    for st in doc.get("strategies") or []:
        if st.get("id") in PICK_SKIP:
            continue
        pick, rank = None, 0
        for i, sym in enumerate(st.get("symbols") or []):
            if sym and sym not in taken:
                pick, rank = sym, i + 1
                break
        if pick:
            taken.add(pick)
        out.append((st, pick, ((rows.get(pick) or {}).get("name") or "") if pick else "", rank))
    return out


def record_text(ev):
    """The screen's measured record in one sentence (finterm's own tests)."""
    if not ev or not ev.get("tested"):
        return "Not backtested."
    # Only what the export holds: no comparison or period it does not state.
    if ev.get("avg_R") is not None:
        n = ev.get("trades")
        return (f"Tested as trades: average {ev['avg_R']:+.2f}R per trade"
                + (f" over {n:,.0f} trades" if n else "") + ".")
    x, t = ev.get("excess"), ev.get("t")
    if x is None:
        return "Not backtested."
    return (f"{x * 100:+.1f}% a year vs the equal-weighted S&P 500"
            + (f" (t {t:.1f})" if t is not None else "") + ".")


def picks_section(picks, asof=None, error=None, stale=0):
    out = ["## Top name from each screen", ""]
    out.append(
        "The first-ranked stock on each of finterm's strategy screens"
        + (f" as of the {asof} close" if asof else "")
        + " — each screen is a published, rule-exact method (Minervini, O'Neil, Greenblatt, Piotroski and "
        "others), applied to US stocks above $2B. **Screen output, not a recommendation.** Every screen was "
        "backtested on point-in-time S&P 500 members from 2010 to 2026, and none beat the equal-weighted index "
        "with statistical significance; each line gives that screen's own record. A name already listed under "
        "an earlier screen passes to that screen's next one."
    )
    if stale and not error:
        out += ["", f"_Prices for {stale} stock{'' if stale == 1 else 's'} could not be refreshed for this run; "
                    "they were ranked on their last earlier close._"]
    out.append("")
    if error:
        out.append(f"_The screens could not be run this week ({md_escape(error)})._")
    elif not picks:
        out.append("_No screen produced a list this week._")
    else:
        for st, sym, company, rank in picks:
            name = md_escape(st.get("name"))
            if not sym:
                out.append(f"- **{name}** — no stock passed this week.")
                continue
            who = f"**{md_escape(sym)}** {md_escape(company)}".rstrip()
            pos = "" if rank == 1 else f" (its #{rank})"
            summary = PUBLIC_SUMMARY.get(st.get("id")) or st.get("summary")
            stop = "" if who.endswith(".") else "."  # "CareDx, Inc." — no second period
            out.append(f"- **{name}**{pos}: {who}{stop} {md_escape(summary)} "
                       f"Ranked by: {md_escape(st.get('rank_by')) or 'its score'}. "
                       f"_Record: {md_escape(record_text(st.get('evidence')))}_")
    out.append("")
    return out
