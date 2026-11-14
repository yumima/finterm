#!/usr/bin/env python3
"""Weekly brief: insider-buying clusters from SEC Form 4 filings, as Markdown.

Built from finterm's own pipeline (sec_form4_market — what the Ownership
screen's market-wide scan runs), so the published rows are the ones the app
shows. Open-market purchases (code P) filed in the last 7 days, grouped per
issuer, two or more distinct insiders, 10%-owners and 10b5-1 plan trades
excluded, sorted by total dollars reported.

Public output uses SEC EDGAR data only (public information, freely
redistributable; cite "U.S. SEC, EDGAR"). The IPO section built from
Nasdaq's calendar is for personal use only — Nasdaq's terms forbid
redistribution — so it is available locally with --include-ipo and never
written to the public site.

Modes:
    python3 tools/weekly_brief.py [--out DIR]          one Markdown file (local)
    python3 tools/weekly_brief.py --site SITE_DIR      the website: an issue page
        (_posts/), the always-current /insider-cluster-buys/ page, and
        SITE_DIR/_data/announce.json with this week's social post texts.

Wording rules (publisher's exclusion — impersonal, bona fide, regular): the
same template every week including empty weeks, neutral verbs ("filed",
"reported"), a mechanical sort key stated in the text, no recommendations.

SEC refuses clients that don't identify themselves: the contact email is read
from FINTERM_CONTACT_EMAIL, else from finterm's keyring entry. It is never
written to the output.
"""

import argparse
import os
import re
import sys
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

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


def ipo_sections(today):
    import ipo_calendar as ipo

    data = ipo.build(today=today, enrich=True)
    horizon = today + timedelta(days=30)
    upcoming = []
    for d in data["deals"]:
        p = d.get("projection") or {}
        if p.get("kind") not in ("scheduled", "imminent", "estimate") or not p.get("date"):
            continue
        when = date.fromisoformat(p["date"])
        # A window (imminent / estimate) is in range when any of it is; its
        # start is never shown in the past.
        lo = max(date.fromisoformat(p.get("lo") or p["date"]), today)
        hi = date.fromisoformat(p.get("hi") or p["date"])
        if lo > horizon or hi < today:
            continue
        p = dict(p, lo=lo.isoformat(), hi=max(hi, lo).isoformat())
        upcoming.append((lo, d, p))
    upcoming.sort(key=lambda t: (t[0], t[2].get("hi") or ""))  # by window start, then end

    out = ["## IPO pipeline — next 30 days", ""]
    out.append(
        "Deals projected to price within 30 days. **Basis** says what each date rests on: *scheduled* is the "
        "underwriters' date from Nasdaq's calendar; *imminent* means an exchange or SEC step (8-A, CERT, EFFECT) "
        "is on file; *estimate* is the pricing lag of comparable filers still unpriced at the same age."
    )
    out.append("")
    if not upcoming:
        out.append("_Nothing projected in the window._")
    else:
        out.append("| Projected | Ticker | Company | Range | Basis |")
        out.append("|---|---|---|---|---|")
        for when, d, p in upcoming[:30]:
            window = when.isoformat()
            if p.get("lo") and p.get("hi") and p["lo"] != p["hi"]:
                window = f"{p['lo']} – {p['hi']}"
            rng = d.get("price_range") or (f"${d['final_price']}" if d.get("final_price") else "—")
            out.append(
                f"| {window} | {md_escape(d.get('ticker')) or '—'} | {md_escape(d.get('company'))} | "
                f"{md_escape(rng)} | {p['kind']}: {md_escape(p.get('basis'))} |"
            )
    out.append("")

    lockups = [l for l in data.get("lockups", []) if today <= date.fromisoformat(l["expiry"]) <= horizon]
    out += ["## Lock-up expiries — next 30 days", ""]
    out.append(
        "When insiders' and early holders' post-IPO lock-up ends. From each deal's own prospectus term where "
        "Nasdaq has it; *assumed* rows use the common 180 days. Early-release clauses and waivers are not modelled."
    )
    out.append("")
    if not lockups:
        out.append("_None in the window._")
    else:
        out.append("| Expiry | Ticker | Company | Priced | Term |")
        out.append("|---|---|---|---|---|")
        for l in lockups:
            term = f"{l['lockup_days']}d" + (" (assumed)" if l["basis"] == "assumed" else "")
            out.append(f"| {l['expiry']} | {md_escape(l['ticker'])} | {md_escape(l['company'])} | "
                       f"{l['priced_date']} | {term} |")
    for e in data.get("errors", []):
        out += ["", f"> {e}"]
    out.append("")
    return out


SITE_URL = "https://yumima.github.io/finterm"


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


def fit_post(head, items, tail, short_tail, url, limit, url_cost):
    """The longest post within @limit: drop list items from the end, then fall
    back to the short tail. @url_cost is what the network charges for the
    link (X and Mastodon: 23 for any URL; Bluesky: its real length)."""
    def cost(t):
        return len(t) - len(url) + url_cost

    for t in (tail, short_tail):
        for k in range(len(items), -1, -1):
            body = head if k == 0 else f"{head} Largest by $ reported: {', '.join(items[:k])}."
            text = f"{body}\n\n{t}"
            if cost(text) <= limit:
                return text
    return f"{head[:limit - cost(short_tail) - 3].rstrip()}…\n\n{short_tail}"


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
    items = []
    for r in rows:
        if len(items) == 3:
            break
        tag = f"${r['symbol']}" if r.get("symbol") else (r.get("issuer") or "")[:24].rstrip(" ,.")
        if not tag:
            continue  # a row with no name says nothing in a post
        items.append(f"{tag} {money(r['value'])} ({r['insiders']} insiders)")
    tail = f"Full list, method and base rate: {url}"
    short = f"Full list: {url}"
    masto_tail = f"{tail}\n\nNot investment advice. #investing #stocks #insidertrading #opensource"
    return {
        "url": url,
        "x": fit_post(head, items, tail, short, url, 280, 23),
        "bluesky": fit_post(head, items, tail, short, url, 300, len(url)),
        "mastodon": fit_post(head, items, masto_tail, short, url, 500, 23),
    }


def page(front, body):
    """A Markdown page: YAML front matter (values JSON-quoted, which YAML
    reads as strings), the body, and the disclaimer."""
    import json

    fm = ["---"] + [f"{k}: {json.dumps(v, ensure_ascii=False) if isinstance(v, str) else v}"
                    for k, v in front.items()] + ["---", ""]
    return "\n".join(fm + body + [f"_{DISCLAIMER}_", ""])


def write_site(site_dir, rows, notes, today):
    import json

    key, url = issue_key(today), issue_url(today)
    posts = site_dir / "_posts"
    posts.mkdir(parents=True, exist_ok=True)
    # A rerun this week replaces the week's issue rather than adding a second.
    for old in posts.glob("*-insider-buying.md"):
        try:
            if issue_key(date.fromisoformat(old.name[:10])) == key:
                old.unlink()
        except ValueError:
            pass

    n = len(rows)
    span = f"the 7 days to {today:%B %-d, %Y}"
    # A per-run stamp in the page lets the publisher wait for THIS version to
    # be live, not an earlier deploy of the same URL.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    # {% raw %}: EDGAR text is data — a "{{" or "{%" in a filer's name must
    # never reach Liquid (it would break the build).
    body = [f"<!-- issue {key} stamp {stamp} -->", "", "{% raw %}"] + clusters_section(rows, notes) + ["{% endraw %}", ""]
    post = posts / f"{issue_day(today).isoformat()}-insider-buying.md"
    post.write_text(page({
        "title": f"Insider buying clusters — {span}",
        "description": (f"{n} {'company' if n == 1 else 'companies'} where two or more insiders reported "
                        f"open-market purchases in SEC Form 4 filings, {span}." if n else
                        f"No insider-buying clusters in SEC Form 4 filings, {span}."),
        # The week's Monday, date only: Jekyll reads it as midnight UTC, so it
        # is never in the future; the post layout shows `updated`.
        "date": issue_day(today),
        "updated": today,
    }, body), encoding="utf-8")

    (site_dir / "insider-cluster-buys.md").write_text(page({
        "title": "Insider buying clusters this week (SEC Form 4)",
        "description": "Companies where two or more insiders reported open-market purchases in the last 7 days "
                       "of SEC Form 4 filings. Updated weekly.",
        "permalink": "/insider-cluster-buys/",
    }, [f"_Updated {today:%B %-d, %Y} · [this week's issue]({url}) · [all issues]({SITE_URL}/brief/)_", ""] + body),
        encoding="utf-8")

    data = site_dir / "_data"
    data.mkdir(parents=True, exist_ok=True)
    (data / "announce.json").write_text(json.dumps(
        {"issue": key, "date": today.isoformat(), "stamp": stamp, **social_posts(rows, today, url)},
        indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return post


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="local mode: output folder (default: finterm's files folder, weekly/)")
    ap.add_argument("--site", help="site mode: the website folder to write into (public: EDGAR data only)")
    ap.add_argument("--include-ipo", action="store_true",
                    help="local mode only: add the IPO pipeline (Nasdaq calendar data — personal use only)")
    ap.add_argument("--no-scan", action="store_true", help="use the Form 4 data already scanned; don't fetch")
    ap.add_argument("--allow-incomplete", action="store_true",
                    help="site mode: publish even when some days of filings could not be read")
    args = ap.parse_args()
    if args.site and args.include_ipo:
        ap.error("--include-ipo is personal-use data and is never published to the site")

    if not os.environ.get(CONTACT_KEY):
        c = contact_from_keyring()
        if c:
            os.environ[CONTACT_KEY] = c
    if not os.environ.get(CONTACT_KEY):
        print("note: no SEC contact email found (Settings › Credentials › Contact email); "
              "SEC may refuse the requests.", file=sys.stderr)

    today = date.today()
    week_of = today - timedelta(days=today.weekday())
    rows, notes, complete = clusters_data(scan=not args.no_scan)

    if args.site:
        if not complete and not args.allow_incomplete:
            # A partial week published and announced reads as a quiet week.
            # Fail loudly; the next run resumes where this one stopped.
            for n in notes:
                print(f"error: {n}", file=sys.stderr)
            print("error: not publishing an incomplete issue (--allow-incomplete overrides)", file=sys.stderr)
            sys.exit(2)
        print(write_site(Path(args.site), rows, notes, today))
        return

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# Weekly brief — week of {week_of:%B %-d, %Y}", ""]
    lines.append(f"_Generated {today.isoformat()} from SEC EDGAR filings"
                 + (" and Nasdaq's IPO calendar (personal use only)._" if args.include_ipo else "._"))
    lines.append("")
    lines += clusters_section(rows, notes)
    if args.include_ipo:
        lines += ipo_sections(today)
    lines += ["---", "", f"_{DISCLAIMER}_", ""]
    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
