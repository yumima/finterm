#!/usr/bin/env python3
"""Weekly brief, one page of Markdown tables: the market at a glance (the call,
the past and the next week), markets and the economy, the week ahead, and stocks (insider buying, fund moves, each strategy screen's first
names).

Built from finterm's own pipeline: SEC Form 4 (sec_form4_market, open-market
purchases filed in the last 7 days, two or more insiders), SEC 13F
(sec_13f_bulk.stock_flows), the screens (screener_data) and the release
calendar, so the published rows are the ones the app shows.

Public output: SEC EDGAR data (public information; cite "U.S. SEC, EDGAR");
figures of U.S. agencies and the Federal Reserve through FRED (public domain
series only); the week's release SCHEDULE (never the feed's consensus); and
each screen's TICKERS with finterm's own backtest record and its own counts of
market breadth — no TradingView or Yahoo number is reproduced (their terms
forbid redistribution). The IPO section built from Nasdaq's calendar is for
personal use only — Nasdaq's terms forbid redistribution — so it is available
locally with --include-ipo and never written to the public site.

Modes:
    python3 tools/weekly_brief.py [--out DIR]          one Markdown file (local)
    python3 tools/weekly_brief.py --site SITE_DIR      the website: an issue page
        (_posts/), the always-current /insider-cluster-buys/ page, and
        SITE_DIR/_data/announce.json with this week's social post texts.

Wording rules (publisher's exclusion — impersonal, bona fide, regular): the
same template every week including empty weeks, neutral verbs ("filed",
"reported"), a mechanical sort key stated in the text, no recommendation of any security —
the screen names are labelled screen output, each beside a record that shows
no screen beat the index with significance. The market stance is a fixed rule's
output, the same for every reader, printed beside the rule's own record.

SEC refuses clients that don't identify themselves: the contact email is read
from FINTERM_CONTACT_EMAIL, else from finterm's keyring entry. It is never
written to the output.
"""

import argparse
import os
import sys
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

from weekly_brief_core import (  # noqa: E402,F401 — the shared brief; re-exported for callers
    CLUSTER_HISTORY, CONTACT_KEY, DISCLAIMER, ISSUE_DISCLAIMER, METHOD_URL, SITE_URL, WINDOW_DAYS,
    brief_body, clusters_data, clusters_section, contact_from_keyring, fetch_macro_rows, fetch_macro_series,
    issue_day, issue_key, issue_url, macro_coverage, macro_events, md_escape, money, page, private_block,
    social_posts, week_ahead,
)


def screener_doc(timeout=900):
    """One scan of the strategy screens, run in finterm's analytics Python
    (pandas, yfinance) — this script runs under the system Python. Returns
    (doc, error-or-None)."""
    import json
    import subprocess

    import finterm_paths

    root = finterm_paths.data_root() / "venv-numpy2"
    py = root / ("Scripts/python.exe" if sys.platform.startswith("win") else "bin/python3")
    if not py.exists():
        return None, "finterm's analytics Python is not installed"
    try:
        r = subprocess.run([str(py), str(SCRIPTS / "screener_data.py"), "scan", "{}"],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "the scan timed out"
    try:
        doc = json.loads(r.stdout.strip().splitlines()[-1]) if r.stdout.strip() else {}
    except ValueError:
        doc = {}
    if not isinstance(doc, dict):  # a stray last line must not take the issue down
        doc = {}
    if not doc or doc.get("error"):
        return None, doc.get("error") or f"the scan failed (exit {r.returncode})"
    return doc, None


ALL_TOPICS = ("summary", "macro", "insider", "funds", "screens", "ipo_private")


def build_brief(today, clusters, notes, topics=ALL_TOPICS, detail="standard", private_ipo=False, extras=True):
    """(body lines, data): the issue's one page. Anything that cannot be read
    is said on the page; none of it holds the issue back. @private_ipo (drafts
    only): the IPO pipeline from Nasdaq's calendar, in a private block that
    publishing strips. Without @extras only the insider section, no fetches."""
    if not extras:
        topics = [t for t in topics if t == "insider"]
    mon, fri = week_ahead(today)
    series, events, macro_err, doc, scr_err, flows = {}, [], None, None, None, None
    if "macro" in topics or "summary" in topics:
        series = fetch_macro_series(today)
        rows, failed = fetch_macro_rows()
        macro_err = macro_coverage(rows, mon, fri, failed)
        events = macro_events(rows, mon, fri)
    if "screens" in topics or "summary" in topics:  # the scan also counts the market's breadth
        doc, scr_err = screener_doc()
    if "funds" in topics:
        try:
            import sec_13f_bulk

            flows = sec_13f_bulk.stock_flows()
        except Exception as e:  # noqa: BLE001
            flows = {"error": type(e).__name__}
    body = brief_body(topics, detail, series, doc, scr_err, clusters, notes, flows, events, mon, fri, macro_err, today)
    if private_ipo and "ipo_private" in topics:
        try:
            ipo = ipo_sections(today, max_rows=8)
        except Exception as e:  # noqa: BLE001 — a private extra never holds the draft back
            ipo = [f"_The IPO calendar could not be read ({type(e).__name__})._", ""]
        body += private_block(["## Private: IPOs ahead (Nasdaq calendar — only you see this; never published)",
                               ""] + ipo)
    for err in (macro_err, scr_err, (flows or {}).get("error")):
        if err:
            print(f"warning: {err}", file=sys.stderr)
    return body, {"series": series, "flows": flows, "screens": bool(doc)}


def ipo_sections(today, max_rows=30):
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
        for when, d, p in upcoming[:max_rows]:
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
        for l in lockups[:max_rows]:
            term = f"{l['lockup_days']}d" + (" (assumed)" if l["basis"] == "assumed" else "")
            out.append(f"| {l['expiry']} | {md_escape(l['ticker'])} | {md_escape(l['company'])} | "
                       f"{l['priced_date']} | {term} |")
    for e in data.get("errors", []):
        out += ["", f"> {e}"]
    out.append("")
    return out


SECTION_WORDS = {"summary": "the market at a glance", "macro": "markets, economy and the week ahead", "insider": "insider buying", "funds": "fund moves",
                 "screens": "screen leaders"}


def write_site(site_dir, rows, notes, today, body, complete=True, topics=ALL_TOPICS, data=None):
    import json

    key, url = issue_key(today), issue_url(today)
    posts = site_dir / "_posts"
    posts.mkdir(parents=True, exist_ok=True)
    # A rerun this week replaces the week's issue rather than adding a second.
    for old in posts.glob("*-insider-buying.md"):
        try:
            # file names carry issue_day (the week's Monday), not a run day
            if old.name[:10] == issue_day(today).isoformat():
                old.unlink()
        except ValueError:
            pass

    n = len(rows)
    # A per-run stamp in the page lets the publisher wait for THIS version to
    # be live, not an earlier deploy of the same URL.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    # {% raw %}: EDGAR text is data — a "{{" or "{%" in a filer's name must
    # never reach Liquid (it would break the build).
    raw = lambda lines: ["{% raw %}"] + list(lines) + ["{% endraw %}", ""]
    mark = [f"<!-- issue {key} stamp {stamp} -->", ""]
    # Title and description name only the sections the issue holds.
    what = ", ".join(w for t, w in SECTION_WORDS.items() if t in topics)
    # The file name stays "-insider-buying": it is how a rerun finds the
    # week's issue, and the announced URL does not depend on it.
    post = posts / f"{issue_day(today).isoformat()}-insider-buying.md"
    post.write_text(page({
        "title": f"Weekly brief, {today:%B %-d, %Y}",
        "description": f"{what[0].upper()}{what[1:]} — the week to {today:%B %-d, %Y}, on one page.",
        # The week's Monday, date only: Jekyll reads it as midnight UTC, so it
        # is never in the future; the post layout shows `updated`.
        "date": issue_day(today),
        "updated": today,
        "compact": True,  # the post layout's one-page style
    }, mark + raw(body), ISSUE_DISCLAIMER), encoding="utf-8")

    # The evergreen insider page: the full list, which the issue links to.
    (site_dir / "insider-cluster-buys.md").write_text(page({
        "title": "Insider buying clusters this week (SEC Form 4)",
        "description": "Companies where two or more insiders reported open-market purchases in the last 7 days "
                       "of SEC Form 4 filings. Updated weekly.",
        "permalink": "/insider-cluster-buys/",
    }, [f"_Updated {today:%B %-d, %Y} · [this week's issue]({url}) · [all issues]({SITE_URL}/brief/)_", ""]
        + mark + raw(clusters_section(rows, notes, max_rows=len(rows)))), encoding="utf-8")

    # complete/notes: the Social screen's draft review shows them and asks
    # before publishing an incomplete week (--allow-incomplete builds one).
    issue = {"issue": key, "date": today.isoformat(), "stamp": stamp, "count": n,
             "complete": bool(complete), "notes": list(notes),
             "rows": rows[:25],
             **social_posts(rows, today, url, topics, (data or {}).get("series"), (data or {}).get("flows"),
                            (data or {}).get("screens", False))}
    out = site_dir / "_data"
    out.mkdir(parents=True, exist_ok=True)
    (out / "announce.json").write_text(json.dumps(issue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    # The same, published beside the pages: the app's Social screen shows and
    # posts exactly this, so a post always matches the page it links to.
    (site_dir / "issue.json").write_text(json.dumps(issue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return post


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="local mode: output folder (default: finterm's files folder, weekly/)")
    ap.add_argument("--site", help="site mode: the website folder to write into (public: EDGAR data only)")
    ap.add_argument("--include-ipo", action="store_true",
                    help="local mode only: add the IPO pipeline (Nasdaq calendar data — personal use only)")
    ap.add_argument("--no-scan", action="store_true", help="use the Form 4 data already scanned; don't fetch")
    ap.add_argument("--no-extras", action="store_true",
                    help="the insider section only (the others fetch: the screener scan takes minutes) — "
                         "e.g. for a quick offline rebuild with --no-scan")
    ap.add_argument("--allow-incomplete", action="store_true",
                    help="site mode: publish even when some days of filings could not be read")
    ap.add_argument("--topics", default=",".join(ALL_TOPICS),
                    help="comma-separated sections: summary, macro, insider, funds, screens, ipo_private (drafts only)")
    ap.add_argument("--detail", default="standard", choices=("full", "standard", "short"),
                    help="rows a table holds: 10, 5 or 3")
    ap.add_argument("--draft", action="store_true",
                    help="site mode, for review in the Social screen: adds the IPO pipeline as a PRIVATE block "
                         "(brief_drafts strips it from anything published)")
    args = ap.parse_args()
    topics = [t.strip() for t in args.topics.split(",") if t.strip()]
    bad = [t for t in topics if t not in ALL_TOPICS]
    if bad:
        ap.error(f"unknown topic(s): {', '.join(bad)}")
    if args.no_extras:
        topics = [t for t in topics if t == "insider"]
    if not [t for t in topics if t in SECTION_WORDS and t != "summary"]:
        ap.error("a brief with nothing in it is not written: name a section"
                 + (" (--no-extras leaves only insider)" if args.no_extras else ""))
    if args.site and args.include_ipo:
        ap.error("--include-ipo is personal-use data and is never published to the site")
    if args.draft and not args.site:
        ap.error("--draft goes with --site")

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
        body, data = build_brief(today, rows, notes, topics, args.detail, private_ipo=args.draft,
                                 extras=not args.no_extras)
        print(write_site(Path(args.site), rows, notes, today, body, complete=complete, topics=topics, data=data))
        return

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# Weekly brief, {today:%B %-d, %Y}", ""]
    lines += build_brief(today, rows, notes, topics, args.detail, extras=not args.no_extras)[0]
    if args.include_ipo:
        lines += ipo_sections(today)
    lines += ["---", "", f"_{ISSUE_DISCLAIMER}_", ""]
    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
