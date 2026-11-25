"""The weekly brief — data, page text and post texts.

Shared by the app (social_app.py, behind the Social screen)
and the owner's publishing tools (tools/weekly_brief.py, tools/announce.py).
The app shows and posts the PUBLISHED issue (issue.json beside the page), so
a post always matches the page it links to.
Public output: SEC EDGAR data, U.S. agencies' and the Fed's figures (FRED), the
week's release schedule, and screen tickers with finterm's own backtest records
and market counts (no vendor figures) — see tools/weekly_brief.py.
"""
import os
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


# The weekly issue says where each of its sections came from (the insider-only
# page keeps DISCLAIMER).
ISSUE_DISCLAIMER = (
    "General, impersonal information compiled mechanically from public data. The market stance and the screens are the "
    "output of fixed rules, the same for every reader: not advice fitted to anyone's circumstances, not a "
    "recommendation of any security, not an offer. finterm and its authors are not registered investment advisers or "
    "broker-dealers and receive no compensation from any company listed. Backtests are hypothetical; past results do "
    "not predict future returns. Data may contain errors: verify against the filings. Sources: SEC EDGAR (Forms 4 and "
    "13F); Federal Reserve, BEA, BLS, DOL and EIA via FRED, Federal Reserve Bank of St. Louis; release dates via the "
    "ForexFactory calendar; the market stance, screens and breadth computed by finterm from S&P 500, VIX, TradingView "
    "and Yahoo Finance data, none of which is reproduced."
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
        if abs(v) >= div * 0.99995:  # 999,999 reads $1.0M, not $1,000.0K
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


def clusters_section(rows, notes, max_rows=25):
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
        for r in rows[:max_rows]:
            avg = f"${r['avg_price']:,.2f}" if r.get("avg_price") else "—"
            dates = r["first_trade"] if r["first_trade"] == r["last_trade"] else f"{r['first_trade']} – {r['last_trade']}"
            out.append(
                f"| {md_escape(r['symbol']) or '—'} | {md_escape(r['issuer'])} | {r['insiders']} | "
                f"{money(r['value'])} | {avg} | {dates} | {md_escape(', '.join(r['roles'][:3]))} |"
            )
        if len(rows) > max_rows:
            out += ["", f"_{len(rows) - max_rows} more companies met the conditions; the {max_rows} largest by "
                        "dollars are shown._"]
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


def _limit(network, url):
    limit, url_cost = NETWORK_LIMITS[network]
    return limit, len(url) if url_cost is None else url_cost


def post_lines(topics, series, rows, flows, max_cashtags, today, screens=True):
    """What moved, a line per section the issue holds, most general first. No
    screen names: beside a ticker alone, a post has no room for the record."""
    lines = []
    r = market_regime(series, today) if "summary" in topics and series else None
    if r:
        lines.append(f"{LABELS[r['state']]}: {regime_words(r)} ({r['date']:%b %-d})")
    if "macro" in topics and series:
        by = {r["name"]: r for r in market_rows(series) if r["value"] != "—"}
        parts = [f"{short} {by[name]['value']} ({by[name]['change']} 1w)" for name, short in
                 (("10-year Treasury", "10Y"), ("2-year Treasury", "2Y")) if name in by]
        eco = {r["name"]: r for r in economy_rows(series) if r["value"] != "—"}
        # (A row whose newest month has no figure is not the latest: left out.)
        parts += [f"{short} {eco[name]['value']}" for name, short in
                  (("CPI, y/y", "CPI y/y"), ("Unemployment rate", "jobless rate"))
                  if name in eco and "no base" not in eco[name]["asof"]]
        if parts:
            lines.append(" · ".join(parts))
    tags = 0

    def tag(sym):
        nonlocal tags
        tags += 1
        return f"${sym}" if tags <= max_cashtags else sym

    if "insider" in topics and rows:
        top = [f"{tag(r['symbol'])} {money(r['value'])}" for r in rows if r.get("symbol")][:3]
        if top:
            lines.append(f"Insiders bought: {', '.join(top)}")
    if "funds" in topics and flows and not flows.get("error"):
        for key, word in (("added", "Funds added, net"), ("trimmed", "Funds trimmed, net")):
            names = [tag(r["ticker"]) for r in (flows.get(key) or [])[:3]]
            if names:
                lines.append(f"{word}: {', '.join(names)}")
    if "screens" in topics and screens:
        lines.append("Plus each strategy screen's first names and record")
    return lines


def fit_lines(head, lines, tail, limit, url, url_cost, weigh=len):
    """The longest post within @limit: lines are dropped from the end."""
    for k in range(len(lines), -1, -1):
        text = "\n".join([head, *lines[:k]]) + f"\n\n{tail}"
        if weigh(text) - weigh(url) + url_cost <= limit:
            return text
    return f"{head}\n\n{url}"


def social_posts(rows, today, url, topics=("summary", "macro", "insider", "funds", "screens"), series=None, flows=None,
                 screens=True):
    """This issue's announcement per network, built from its own figures and
    only from the sections it holds."""
    head = f"What's moving, week to {today:%b %-d}"
    out = {"url": url}
    for net, extra, weigh in (("x", "", x_weight), ("bluesky", "", len),
                              ("mastodon", "\n\nFixed rules, not personal advice. #investing #stocks #opensource", len)):
        # X refuses a post with more than one cashtag (HTTP 403); the others take any.
        lines = post_lines(topics, series or {}, rows, flows, MAX_CASHTAGS.get(net, 99), today,
                           screens)
        out[net] = fit_lines(head, lines, url + extra, _limit(net, url)[0], url, _limit(net, url)[1], weigh)
    return out


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


# ── the strategy screens ─────────────────────────────────────────────────────
#
# Screen output, not a recommendation. Published: tickers, finterm's OWN
# backtest records and its own counts over the market (breadth). The metrics
# behind the rankings come from TradingView and Yahoo, whose terms do not allow
# redistribution, so no price, ratio or return of theirs is written.

# RS Leaders is the Trend Template's top 25 by RS: both lists start with the
# same names every week, so the template is represented by RS Leaders.
PICK_SKIP = ("trend_template",)


# ── private sections ─────────────────────────────────────────────────────────
#
# A draft is read by its owner first: it may carry sections built from data
# that must not be redistributed (Nasdaq's IPO calendar). They sit between
# these markers and strip_private() removes them from anything published —
# brief_drafts.publish() calls it on every file it copies.

PRIVATE_OPEN, PRIVATE_CLOSE = "<!-- private -->", "<!-- /private -->"


def private_block(lines):
    return [PRIVATE_OPEN] + list(lines) + [PRIVATE_CLOSE, ""]


def strip_private(text):
    """@text without its private blocks. An unclosed block runs to the end:
    a marker the reader damaged must fail closed, never publish the rest."""
    out, hidden = [], False
    for ln in text.split("\n"):
        s = ln.strip()
        if s == PRIVATE_OPEN:
            hidden = True
            continue
        if s == PRIVATE_CLOSE:
            hidden = False
            continue
        if not hidden:
            out.append(ln)
    return "\n".join(out)


# ── macro and economy ────────────────────────────────────────────────────────
#
# Public-domain figures of U.S. agencies and the Federal Reserve, read through
# FRED. Series with a private owner (S&P 500, VIX, ICE spreads) are not used:
# they may not be republished.

# id: (name, kind, unit). Kinds: level = the value, weekly change beside it;
# yoy = 12-month change of an index; diff = change on the month before.
MARKET_SERIES = {
    "DGS10": ("10-year Treasury", "bp"),
    "DGS2": ("2-year Treasury", "bp"),
    "DFF": ("Fed funds, effective", "bp"),
    "DTWEXBGS": ("Broad dollar index", "pct"),
    "DCOILWTICO": ("WTI crude, $/bbl", "pct"),
    "NFCI": ("Financial conditions, NFCI", "abs"),
}
ECONOMY_SERIES = {
    "A191RL1Q225SBEA": ("Real GDP, q/q annualised", "pct_level"),
    "CPIAUCNS": ("CPI, y/y", "yoy"),  # unadjusted: the 12-month figure BLS reports
    "PCEPILFE": ("Core PCE, y/y", "yoy"),
    "UNRATE": ("Unemployment rate", "pct_level"),
    "PAYEMS": ("Payrolls, monthly change", "diff_k"),
    "ICSA": ("Initial jobless claims", "thousands"),
}
SPARK = "▁▂▃▄▅▆▇█"


def spark(values, n=12):
    """The last values as a line of blocks, lowest to highest of those shown.
    "" for fewer than three; a flat line is drawn in the middle."""
    v = list(values)
    if len(v) > n:  # evenly spaced, the newest always the last
        v = [v[round(i * (len(v) - 1) / (n - 1))] for i in range(n)]
    if len(v) < 3:
        return ""
    lo, hi = min(v), max(v)
    if hi == lo:
        return SPARK[3] * len(v)
    return "".join(SPARK[round((x - lo) / (hi - lo) * (len(SPARK) - 1))] for x in v)


def fetch_macro_series(today, get=None):
    """{series id: [(date, value)]}, oldest first, two and a half years back.
    A series that could not be read is absent — never an empty history."""
    if get is None:
        import fred_data

        get = fred_data.get_series_keyless
    start = (today - timedelta(days=930)).isoformat()
    out = {}
    for sid in (*MARKET_SERIES, *ECONOMY_SERIES, *REGIME_SERIES):
        try:
            r = get(sid, start)
        except Exception as e:  # noqa: BLE001 — one series never holds the issue back
            r = {"error": type(e).__name__}
        obs = [(date.fromisoformat(o["date"]), float(o["value"])) for o in (r or {}).get("observations") or []]
        if not (r or {}).get("error") and obs:
            out[sid] = sorted(obs)
    return out


def _before(obs, day):
    """The last (date, value) on or before @day, or None."""
    got = [o for o in obs if o[0] <= day]
    return got[-1] if got else None


def _signed(x, fmt, unit=""):
    """A signed figure with a true minus; a change that rounds to nothing is 0."""
    s = format(abs(x), fmt)
    return (s if float(s.replace(",", "")) == 0 else ("+" if x > 0 else "−") + s) + unit


def _minus(s):
    return s.replace("-", "−")


def _change(now, was, kind):
    if was is None:
        return "—"
    if kind == "bp":
        return _signed((now - was) * 100, ".0f", " bp")
    if kind == "pct":
        return _signed((now / was - 1) * 100, ".1f", "%") if was > 0 else "—"
    return _signed(now - was, ".2f")


def market_rows(series):
    """[{name, value, change, trend, asof}]: the latest, its change over a week
    (the last figure at least seven days older), a year's trend. A series that
    was not read keeps its row, as "—"."""
    rows = []

    def add(name, obs, kind, show):
        if not obs:
            rows.append({"name": name, "value": "—", "change": "—", "trend": "", "asof": ""})
            return
        d, v = obs[-1]
        was = _before(obs, d - timedelta(days=7))
        year = [x for day, x in obs if day > d - timedelta(days=365)]
        rows.append({"name": name, "value": show(v), "change": _change(v, was and was[1], kind),
                     "trend": spark(year), "asof": f"{d:%b %-d}"})

    pct = lambda v: _minus(f"{v:.2f}%")
    for sid, (name, kind) in MARKET_SERIES.items():
        show = (pct if kind == "bp" else (lambda v: _minus(f"{v:.2f}")) if kind == "pct"
                else lambda v: _signed(v, ".2f") + (" loose" if round(v, 2) < 0 else " tight" if round(v, 2) > 0 else ""))
        add(name, series.get(sid), kind, show)
        if sid == "DGS2":
            # The spread, on the days both were quoted.
            two = dict(series.get("DGS2") or [])
            both = [(d, v - two[d]) for d, v in series.get("DGS10") or [] if d in two]
            add("10-year minus 2-year", both, "bp", lambda v: _signed(v * 100, ".0f", " bp"))
    return rows


def _month_back(d, n):
    y, m = divmod(d.year * 12 + d.month - 1 - n, 12)
    return date(y, m + 1, 1)


def economy_points(obs, kind):
    """The figure a release reports, per period. y/y and monthly changes are
    taken between the months themselves: a month the agency did not publish
    makes no figure, instead of a change over a longer span."""
    by = {d: v for d, v in obs}
    if kind == "yoy":
        return [(d, (v / by[_month_back(d, 12)] - 1) * 100) for d, v in obs if by.get(_month_back(d, 12))]
    if kind == "diff_k":
        return [(d, v - by[_month_back(d, 1)]) for d, v in obs if _month_back(d, 1) in by]
    return list(obs)


def economy_rows(series):
    """[{name, value, prior, trend, asof}], a row per release. "no base" in the
    period: the newest month has no 12-month change (see below)."""
    show = {"pct_level": lambda v: _minus(f"{v:.1f}%"), "yoy": lambda v: _minus(f"{v:.1f}%"),
            "diff_k": lambda v: _signed(v, ",.0f", "K"), "thousands": lambda v: f"{v / 1000:,.0f}K"}
    rows = []
    for sid, (name, kind) in ECONOMY_SERIES.items():
        pts = economy_points(series.get(sid) or [], kind)
        if not pts:
            rows.append({"name": name, "value": "—", "prior": "—", "trend": "", "asof": ""})
            continue
        d, v = pts[-1]
        asof = (f"Q{(d.month - 1) // 3 + 1} {d.year}" if sid == "A191RL1Q225SBEA"
                else f"{d:%b %-d}" if sid == "ICSA" else f"{d:%b}")
        # A newer month is out but makes no figure: the index a year earlier was
        # never published (October 2025, the shutdown). Said, or the row reads
        # as stale.
        if kind == "yoy" and series[sid][-1][0] > d:
            asof += f" ({series[sid][-1][0]:%b}: no base)"
        recent = pts[-8:] if sid == "A191RL1Q225SBEA" else [p for p in pts if p[0] > d - timedelta(days=370)]
        # The release before, not merely the figure before: after a month that
        # made none there is no prior.
        step = {"A191RL1Q225SBEA": 3, "ICSA": 0}.get(sid, 1)
        prior = pts[-2] if len(pts) > 1 and (not step or pts[-2][0] == _month_back(d, step)) else None
        rows.append({"name": name, "value": show[kind](v), "prior": show[kind](prior[1]) if prior else "—",
                     "trend": spark([x for _, x in recent]), "asof": asof})
    return rows


def economy_section(series):
    out = ["## Markets and economy", ""]
    if not series:
        out += ["_The figures could not be read this week._", ""]
    else:
        out += ["| Market | Latest | 1 week | 12 months | As of |", "|---|---:|---:|---|---|"]
        out += [f"| {r['name']} | {r['value']} | {r['change']} | {r['trend']} | {r['asof']} |" for r in market_rows(series)]
        out += ["", "| Economy | Latest | Prior | Trend | Period |", "|---|---:|---:|---|---|"]
        out += [f"| {r['name']} | {r['value']} | {r['prior']} | {r['trend']} | {r['asof']} |" for r in economy_rows(series)]
        out.append("")
    return out


# ── the market call ──────────────────────────────────────────────────────────
#
# Two inputs, each a conventional level: the S&P 500 against its 200-day
# average, and the VIX against 20 and 30. What each state was followed by since
# 1990 is measured by plans/research/market_regime/regime_backtest.py with THIS
# function and written to regime_evidence.json; the call says only what that
# supports. Neither series is published (private owners): the state is.

REGIME_SERIES = ("SP500", "VIXCLS")
REGIMES = ("bullish", "bullish_nervous", "bearish", "fear")


def regime_state(price, average200, vix):
    if price > average200:
        return "bullish" if vix < 20 else "bullish_nervous"
    return "fear" if vix >= 30 else "bearish"


# The stance is one rule, tested as a rule: all of the money meant for stocks
# above the 200-day average, half of it below (the rest in T-bills). It gave up
# about a point a year for a worst fall a third smaller; out altogether cost
# more, and half only when bearish hardly helped (regime_evidence.json: rule,
# always, compared). The VIX changes the odds quoted, not the stance.
STANCE = {"bullish": 1.0, "bullish_nervous": 1.0, "bearish": 0.5, "fear": 0.5}
LABELS = {"bullish": "Bullish", "bullish_nervous": "Bullish, with nerves", "bearish": "Bearish", "fear": "High fear"}
EVIDENCE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "regime_evidence.json")


def market_regime(series, today):
    """{state, date, price_gap, vix, week} on the last close both series hold,
    or None when that is over four days old (a holiday weekend) or the 200 closes
    are not there."""
    px, vix = series.get("SP500") or [], dict(series.get("VIXCLS") or [])
    i = next((k for k in range(len(px) - 1, -1, -1) if px[k][0] in vix), None)
    if i is None or i < 199 or (today - px[i][0]).days > 4:
        return None
    average = sum(v for _, v in px[i - 199:i + 1]) / 200
    was = _before(px[:i], px[i][0] - timedelta(days=7))
    return {"state": regime_state(px[i][1], average, vix[px[i][0]]), "date": px[i][0],
            "price_gap": px[i][1] / average - 1, "vix": vix[px[i][0]],
            "week": px[i][1] / was[1] - 1 if was else None}


def regime_words(r):
    trend = "above" if r["price_gap"] > 0 else "below"
    fear = "low" if r["vix"] < 20 else "elevated" if r["vix"] < 30 else "high"
    return f"S&P 500 {trend} its 200-day average, fear {fear}"


def _load_evidence():
    import json

    try:
        with open(EVIDENCE_PATH, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def call_lines(r, ev):
    """The call, and the record it rests on. Without a measured record there is
    no stance: the state is said and that is all."""
    st, al = ((ev or {}).get("states") or {}).get(r["state"]), (ev or {}).get("all")
    rule, always = (ev or {}).get("rule"), (ev or {}).get("always")
    where = f"{regime_words(r)}, at the {r['date']:%b %-d} close"
    if not (st and al and rule and always):
        return [f"**{LABELS[r['state']]}.** {where}.", ""]
    stance = "full risk" if STANCE[r["state"]] == 1 else "half risk"
    pc = lambda x: f"{x * 100:.0f}%"
    out = [f"**{LABELS[r['state']]}: {stance}.** {where[0].upper()}{where[1:]}. A 10% fall within a quarter has followed "
           f"{pc(st['dd10'])} of such weeks (all weeks: {pc(al['dd10'])}).", ""]
    # Which line the close would have to cross for another call.
    near = []
    if abs(r["price_gap"]) < 0.01:
        near.append("within 1% of the average")
    level = 20 if r["price_gap"] > 0 else 30
    if abs(r["vix"] - level) < 1:
        near.append("the VIX a point from its line")
    out += [f"Since {ev['from']} ({st['weeks']:,} such weeks), S&P 500 with dividends a quarter later: higher "
            f"{pc(st['up'])} of the time (all: {pc(al['up'])}), average {_signed(st['mean'] * 100, '.1f', '%')} "
            f"({_signed(al['mean'] * 100, '.1f', '%')}); average weekly move {st['week_move'] * 100:.1f}%. The stance's "
            "rule, full above the average and half below with the rest in T-bills (the half chosen on the same data): "
            f"{rule['cagr'] * 100:.1f}% a year, worst fall {pc(-rule['worst'])}; always invested {always['cagr'] * 100:.1f}%, {pc(-always['worst'])}."
            + (f" Borderline: {' and '.join(near)}." if near else ""), ""]
    return out


def past_week_line(series, market, r, today, asof=None):
    parts = []
    if r and r["week"] is not None:
        w = r["week"]
        parts.append("S&P 500 " + ("little changed" if abs(w) < 0.0025 else "up" if w > 0 else "down")
                     + f" to {r['date']:%b %-d}")
    m = market or {}
    if m.get("up5_share") is not None:
        parts.append(f"{m['up5_share'] * 100:.0f}% of US stocks above $2B rose"
                     + (f" to {date.fromisoformat(asof):%b %-d}" if asof and r and asof != r["date"].isoformat() else "")
                     + (f", {m['above200_share'] * 100:.0f}% are above their 200-day average"
                        if m.get("above200_share") is not None else ""))
    sec = m.get("sectors") or []
    if len(sec) >= 4:
        parts.append(f"strongest {md_escape(sec[0])}, weakest {md_escape(sec[-1])} (median stock)")
    for sid, short in (("DGS10", "10-year"), ("DTWEXBGS", "dollar"), ("DCOILWTICO", "oil")):
        obs = series.get(sid) or []
        # only a change of the past week: oil's source can be days behind
        if obs and (today - obs[-1][0]).days <= 5:
            name, kind = MARKET_SERIES[sid]
            was = _before(obs, obs[-1][0] - timedelta(days=7))
            parts.append(f"{short} {_change(obs[-1][1], was and was[1], kind)}")
    return f"**Past week:** {'; '.join(parts)}." if parts else None


def week_ahead_line(events, macro_err):
    if macro_err:
        return "**Week ahead:** the release calendar was not available."
    if not events:
        return "**Week ahead:** no top-tier US release."
    order = {"FED": 0, "INFLATION": 1, "LABOR": 2, "GROWTH": 3, "": 4}
    seen, picks = set(), []
    for rank, when, t in sorted(((order[macro_kind(t)], ev["when"], t) for ev in events for t in ev["titles"]),
                                key=lambda x: (x[0], x[1])):
        short = re.sub(r"\s+[mqy]/[mqy]$", "", t)
        if short not in seen:
            seen.add(short)
            picks.append(f"{md_escape(short)} ({when:%a})")
    return f"**Week ahead:** {', '.join(picks[:3])}" + (f" and {len(picks) - 3} more." if len(picks) > 3 else ".")


def glance_section(series, market, events, macro_err, today, evidence=None, asof=None):
    """The top of the page: the market call, the week past, the week ahead."""
    r = market_regime(series, today)
    out = ["## At a glance", ""]
    out += (call_lines(r, _load_evidence() if evidence is None else evidence) if r
            else ["_No market call: this week's closes could not be read._", ""])
    for ln in (past_week_line(series, market, r, today, asof), week_ahead_line(events, macro_err)):
        if ln:
            out += [ln, ""]
    return out


# ── stocks: insiders, funds and the screens, one section ─────────────────────
#
# Each part is a table whose first header names it, and a line of small print.

# Rows a table of the issue holds, and names per screen, at the owner's level
# of detail. The evergreen insider page keeps clusters_section's full list.
BRIEF_ROWS = {"short": 3, "standard": 5, "full": 10}
BRIEF_PICKS = {"short": 1, "standard": 3, "full": 5}
BRIEF_DAYS = 5


def insider_part(rows, notes, max_rows=5):
    n = len(rows)
    out = []
    if not n:
        out += ["_Insider buying: no company had two or more insiders buying $50K or more on the open market (Form 4, 7 days)._"]
    else:
        out += ["| Insider buying | Company | Insiders | Reported |", "|---|---|---:|---:|"]
        out += [f"| {md_escape(r['symbol']) or '—'} | {md_escape(r['issuer'])} | {r['insiders']} | {money(r['value'])} |"
                for r in rows[:max_rows]]
        out += ["", f"{n} {'company' if n == 1 else 'companies'} with two or more insiders buying on the open market, "
                    f"$50K or more (Form 4, 7 days); largest by dollars. [Full list]({SITE_URL}/insider-cluster-buys/). "
                    "In finterm's 2025–26 study a looser form of this signal beat SPY by a median 1.8% over the next "
                    "month (a lone insider's buy: 1.2%) and trailed it by 0.2% over three "
                    f"([method]({METHOD_URL}))."]
    for x in notes:
        out += ["", f"> {x}"]
    return out + [""]


def funds_part(flows, max_rows=5):
    """Institutions' net changes by stock, newest 13F filings (SEC)."""
    out = []
    adds, trims = (flows or {}).get("added") or [], (flows or {}).get("trimmed") or []
    if (flows or {}).get("error") or not (adds or trims):
        why = (flows or {}).get("error")
        return [f"_Fund moves: {md_escape(why) if why else 'none this week'}._", ""]

    def cell(rows, i):
        if i >= len(rows):
            return "", "", ""
        r = rows[i]
        sign = "+" if r["net"] > 0 else "−"
        return md_escape(r["ticker"]), f"{sign}{money(abs(r['net']))}", f"{r['adds']} / {r['trims']}"

    out += ["| Funds added | Net | Adds / cuts | Funds trimmed | Net | Adds / cuts |", "|---|---:|---:|---|---:|---:|"]
    for i in range(min(max_rows, max(len(adds), len(trims)))):
        out.append("| " + " | ".join(cell(adds, i) + cell(trims, i)) + " |")
    q = date.fromisoformat(flows["quarter"])
    filed = flows.get("filed_to")
    out += ["", f"13F positions at {q:%b %-d, %Y}"
                + (f", filed to {date.fromisoformat(filed):%b %-d}" if filed else "")
                + (f", {flows['managers']:,} managers" if flows.get("managers") else "")
                + ", against each manager's previous filing. Counted: US-listed companies (no funds), changes of "
                  "$25M or more by managers with at most 1,000 positions; dollars are share changes at the filings' "
                  "own prices. Splits and new listings are taken out."]
    return out + [""]


def screens_part(doc, error=None, per_screen=3, rows=(), flows=None):
    """Every screen's first names and its measured record, one line each; then
    the names more than one of the three parts holds."""
    if error or not doc:
        return [f"_The screens could not be run this week ({md_escape(error or 'no result')})._", ""]
    out = ["| Screen leaders | First names | Record a year (t) |", "|---|---|---:|"]
    on = {}
    for st in doc.get("strategies") or []:
        if st.get("id") in PICK_SKIP:
            continue
        syms = [s for s in st.get("symbols") or [] if s]
        for s in syms:
            on.setdefault(s, []).append(st["id"])
        name = md_escape((st.get("name") or "").split(" (")[0])
        out.append(f"| {name} | {', '.join(md_escape(s) for s in syms[:per_screen]) or '—'} | "
                   f"{record_short(st.get('evidence'))} |")
    many = sorted((s for s, ids in on.items() if len(ids) >= 3), key=lambda s: (-len(on[s]), s))[:8]
    both = lambda names: [s for s in dict.fromkeys(names) if s in on][:6]
    lines = [("On three or more screens", many),
             ("On a screen, insiders buying", both(r.get("symbol") for r in rows)),
             ("On a screen, funds adding", both(r["ticker"] for r in (flows or {}).get("added") or []))]
    out.append("")
    said = [f"**{what}:** {', '.join(f'{md_escape(s)} ({len(on[s])})' for s in names)}."
            for what, names in lines if names]
    if said:
        out += [" ".join(said), ""]
    stale = int(doc.get("stale_prices") or 0)
    best = max((st["evidence"]["t"] for st in doc.get("strategies") or []
                if (st.get("evidence") or {}).get("t") is not None), default=None)
    out += [(f"As of the {md_escape(doc.get('asof'))} {'session, before its close' if doc.get('intraday') else 'close'}. " if doc.get("asof") else "")
            + "Screen output, not a recommendation. Record: each screen against the equal-weighted S&P 500 in "
              "finterm's backtest (R = per trade, in units of risk)"
            + ("; none beat it with statistical significance." if best is None or best < 2 else ".")
            + (f" {stale} stock{'' if stale == 1 else 's'} ranked on an earlier close." if stale else ""), ""]
    return out


def record_short(ev):
    if not ev or not ev.get("tested"):
        return "not tested"
    if ev.get("avg_R") is not None:
        return _signed(ev["avg_R"], ".2f", "R")
    if ev.get("excess") is None:
        return "not tested"
    t = ev.get("t")
    return _signed(ev["excess"] * 100, ".1f", "%") + (f" ({_signed(t, '.1f')})" if t is not None else "")


def week_ahead_section(events, mon, fri, error=None):
    out = [f"## Week ahead, {fri:%b %-d}" if mon == fri else f"## Week ahead, {mon:%b %-d}–{fri:%-d}"
           if mon.month == fri.month else f"## Week ahead, {mon:%b %-d}–{fri:%b %-d}", ""]
    if error:
        return out + [f"_No release list this week: {md_escape(error)}._", ""]
    if not events:
        return out + ["_No top-tier US release is scheduled._", ""]
    days = {}
    for ev in events:
        w = ev["when"]
        names = []
        for t in ev["titles"]:
            short = re.sub(r"\s+[mqy]/[mqy]$", "", t)  # "CPI m/m" and "CPI y/y" are one release
            if short not in names:
                names.append(short)
        at = "" if (w.hour, w.minute) == (0, 0) else f"{w:%-H:%M} "
        days.setdefault(w.date(), []).append(at + ", ".join(md_escape(x) for x in names))
    out += ["| Day | US releases (Eastern time) |", "|---|---|"]
    out += [f"| {d:%a %-d} | {' · '.join(days[d])} |" for d in sorted(days)[:BRIEF_DAYS]]
    return out + [""]


def stocks_section(topics, detail, doc, scr_err, rows, notes, flows):
    n = BRIEF_ROWS.get(detail, BRIEF_ROWS["standard"])
    out = []
    if "insider" in topics:
        out += insider_part(rows, notes, n)
    if "funds" in topics:
        out += funds_part(flows, n)
    if "screens" in topics:
        out += screens_part(doc, scr_err, BRIEF_PICKS.get(detail, BRIEF_PICKS["standard"]),
                            rows if "insider" in topics else (), flows if "funds" in topics else None)
    return ["## Stocks", ""] + out if out else []


def brief_body(topics, detail, series, doc, scr_err, rows, notes, flows, events, mon, fri, macro_err, today):
    """The issue's sections, one page at the standard detail: the market at a
    glance (the call, past week, next week), markets and economy, the week
    ahead, and stocks (insiders, funds, the screens)."""
    out = []
    if "summary" in topics:
        out += glance_section(series, (doc or {}).get("market"), events, macro_err, today, asof=(doc or {}).get("asof"))
    if "macro" in topics:
        out += economy_section(series)
        out += week_ahead_section(events, mon, fri, macro_err)
    return out + stocks_section(topics, detail, doc, scr_err, rows, notes, flows)
