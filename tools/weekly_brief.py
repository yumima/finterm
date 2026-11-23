#!/usr/bin/env python3
"""Weekly brief: insider-buying clusters from SEC Form 4 filings, the week's
top-tier US macro releases and the top name from each strategy screen, as
Markdown.

Built from finterm's own pipeline (sec_form4_market — what the Ownership
screen's market-wide scan runs), so the published rows are the ones the app
shows. Open-market purchases (code P) filed in the last 7 days, grouped per
issuer, two or more distinct insiders, 10%-owners and 10b5-1 plan trades
excluded, sorted by total dollars reported.

Public output: SEC EDGAR data (public information, freely redistributable;
cite "U.S. SEC, EDGAR"); the week's release SCHEDULE (day, time, name — dates
the agencies publish; the feed's consensus figures are never written); and the
top-ranked TICKER of each screen with the screen's rule and finterm's own
backtest record — no TradingView or Yahoo number is reproduced (their terms
forbid redistribution). The IPO section built from
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
"reported"), a mechanical sort key stated in the text, no recommendations —
the screen names are labelled screen output, each beside a record that shows
no screen beat the index with significance.

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
    CLUSTER_HISTORY, CONTACT_KEY, DETAIL_PICKS, DETAIL_ROWS, DISCLAIMER, ISSUE_DISCLAIMER, METHOD_URL, SITE_URL,
    WINDOW_DAYS,
    clusters_data, clusters_section, contact_from_keyring, issue_day, issue_key, issue_url,
    fetch_macro_rows, macro_coverage, macro_events, private_block, week_summary, macro_section, md_escape, money, page, picks_section, screen_picks,
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


ALL_TOPICS = ("summary", "insider", "macro", "screens", "ipo_private")


def extra_sections(today, clusters=(), private_ipo=False, topics=ALL_TOPICS, detail="standard"):
    """(summary, sections): "The week in brief" (last week / next week /
    screens), then the week ahead (top-tier US macro) and the top name from
    each screen. Either one failing is said on the page; neither holds the
    issue back. @private_ipo (drafts only): the IPO pipeline from Nasdaq's
    calendar, in a private block that publishing strips."""
    mon, fri = week_ahead(today)
    out, events, macro_err = [], [], None
    if "macro" in topics:
        rows, failed = fetch_macro_rows()
        macro_err = macro_coverage(rows, mon, fri, failed)
        events = macro_events(rows, mon, fri)
        # Last week (the insider section, before these) → next week (releases,
        # then the private IPO calendar) → the screens.
        out += macro_section(events, mon, fri, error=macro_err)
    if private_ipo:
        try:
            ipo = ipo_sections(today)
        except Exception as e:  # noqa: BLE001 — a private extra never holds the draft back
            ipo = [f"_The IPO calendar could not be read ({type(e).__name__})._", ""]
        out += private_block(["## Private: IPOs ahead (Nasdaq calendar — only you see this; never published)",
                              ""] + ipo)
    picks, scr_err = [], None
    if "screens" in topics:
        doc, scr_err = screener_doc()
        picks = screen_picks(doc) if doc else []
        cap = DETAIL_PICKS.get(detail)
        if cap is not None:
            picks = [p for p in picks if p[1]][:cap]  # the screens with a name, up to the cap
        out += picks_section(picks, asof=(doc or {}).get("asof"), error=scr_err,
                             stale=int((doc or {}).get("stale_prices") or 0))
    for err in (macro_err, scr_err):
        if err:
            print(f"warning: {err}", file=sys.stderr)
    summary = (week_summary(clusters, events, mon, fri, macro_err, picks, scr_err,
                            include=[t for t in ("insider", "macro", "screens") if t in topics])
               if "summary" in topics else [])
    return summary, out


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


def write_site(site_dir, rows, notes, today, extra=(), complete=True, summary=(), topics=ALL_TOPICS,
               detail="standard"):
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
    span = f"the 7 days to {today:%B %-d, %Y}"
    # A per-run stamp in the page lets the publisher wait for THIS version to
    # be live, not an earlier deploy of the same URL.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    # {% raw %}: EDGAR text is data — a "{{" or "{%" in a filer's name must
    # never reach Liquid (it would break the build).
    insider = ["{% raw %}"] + clusters_section(rows, notes) + ["{% endraw %}", ""]
    # The issue shows the clusters at the owner's detail level, or not at all;
    # the evergreen page below always keeps the full list.
    issue_insider = (["{% raw %}"] + clusters_section(rows, notes, DETAIL_ROWS.get(detail, 25)) + ["{% endraw %}", ""]
                     if "insider" in topics else [])
    # The issue: insider clusters, then the week ahead and the screens. The
    # evergreen insider page keeps the clusters alone.
    body = [f"<!-- issue {key} stamp {stamp} -->", ""] + (
        ["{% raw %}"] + list(summary) + ["{% endraw %}", ""] if summary else []) + issue_insider + (
        ["{% raw %}"] + list(extra) + ["{% endraw %}", ""] if extra else [])
    # The file name stays "-insider-buying": it is how a rerun finds the
    # week's issue, and the announced URL does not depend on it.
    post = posts / f"{issue_day(today).isoformat()}-insider-buying.md"
    post.write_text(page({
        # Title and description name only the sections the issue holds.
        "title": "Weekly brief: " + ", ".join(
            {"insider": "insider buying", "macro": "the week ahead", "screens": "screen leaders"}[t]
            for t in ("insider", "macro", "screens") if t in topics) + f" — {span}",
        "description": ((f"{n} {'company' if n == 1 else 'companies'} where two or more insiders reported "
                         f"open-market purchases in SEC Form 4 filings, {span}." if n else
                         f"No insider-buying clusters in SEC Form 4 filings, {span}.")
                        if "insider" in topics else f"The weekly brief, {span}.")
                       + ((" Plus " if "insider" in topics else " ") + " and ".join(
                           p for t, p in (("macro", "the week's top-tier US releases"),
                                          ("screens", "the top name from each strategy screen"))
                           if t in topics) + "." if extra and ({"macro", "screens"} & set(topics)) else ""),
        # The week's Monday, date only: Jekyll reads it as midnight UTC, so it
        # is never in the future; the post layout shows `updated`.
        "date": issue_day(today),
        "updated": today,
    }, body, ISSUE_DISCLAIMER if extra else DISCLAIMER), encoding="utf-8")

    (site_dir / "insider-cluster-buys.md").write_text(page({
        "title": "Insider buying clusters this week (SEC Form 4)",
        "description": "Companies where two or more insiders reported open-market purchases in the last 7 days "
                       "of SEC Form 4 filings. Updated weekly.",
        "permalink": "/insider-cluster-buys/",
    }, [f"_Updated {today:%B %-d, %Y} · [this week's issue]({url}) · [all issues]({SITE_URL}/brief/)_", ""]
        + [f"<!-- issue {key} stamp {stamp} -->", ""] + insider),
        encoding="utf-8")

    # complete/notes: the Social screen's draft review shows them and asks
    # before publishing an incomplete week (--allow-incomplete builds one).
    issue = {"issue": key, "date": today.isoformat(), "stamp": stamp, "count": n,
             "complete": bool(complete), "notes": list(notes),
             "rows": rows[:25], **social_posts(rows, today, url, topics)}
    data = site_dir / "_data"
    data.mkdir(parents=True, exist_ok=True)
    (data / "announce.json").write_text(json.dumps(issue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
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
                    help="leave out the week-ahead calendar and the screens (they fetch: the screener scan "
                         "takes minutes) — e.g. for a quick offline rebuild with --no-scan")
    ap.add_argument("--allow-incomplete", action="store_true",
                    help="site mode: publish even when some days of filings could not be read")
    ap.add_argument("--topics", default=",".join(ALL_TOPICS),
                    help="comma-separated sections: summary, insider, macro, screens, ipo_private (drafts only)")
    ap.add_argument("--detail", default="standard", choices=("full", "standard", "short"),
                    help="how many rows each section shows")
    ap.add_argument("--draft", action="store_true",
                    help="site mode, for review in the Social screen: adds the IPO pipeline as a PRIVATE block "
                         "(brief_drafts strips it from anything published)")
    args = ap.parse_args()
    topics = [t.strip() for t in args.topics.split(",") if t.strip()]
    bad = [t for t in topics if t not in ALL_TOPICS]
    if bad:
        ap.error(f"unknown topic(s): {', '.join(bad)}")
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
        summary, extra = ((), ()) if args.no_extras else extra_sections(
            today, rows, private_ipo=args.draft and "ipo_private" in topics, topics=topics, detail=args.detail)
        print(write_site(Path(args.site), rows, notes, today, extra, complete=complete, summary=summary,
                         topics=topics, detail=args.detail))
        return

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# Weekly brief — week of {week_of:%B %-d, %Y}", ""]
    lines.append(f"_Generated {today.isoformat()} from SEC EDGAR filings"
                 + (" and Nasdaq's IPO calendar (personal use only)._" if args.include_ipo else "._"))
    lines.append("")
    summary, extra = ((), ()) if args.no_extras else extra_sections(today, rows, topics=topics, detail=args.detail)
    lines += list(summary) + (clusters_section(rows, notes, DETAIL_ROWS.get(args.detail, 25))
                              if "insider" in topics else []) + list(extra)
    if args.include_ipo:
        lines += ipo_sections(today)
    lines += ["---", "", f"_{DISCLAIMER if args.no_extras else ISSUE_DISCLAIMER}_", ""]
    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
