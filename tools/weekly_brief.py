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
import sys
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

from weekly_brief_core import (  # noqa: E402,F401 — the shared brief; re-exported for callers
    CLUSTER_HISTORY, CONTACT_KEY, DISCLAIMER, METHOD_URL, SITE_URL, WINDOW_DAYS,
    clusters_data, clusters_section, contact_from_keyring, issue_day, issue_key, issue_url,
    md_escape, money, page, social_posts,
)


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

    issue = {"issue": key, "date": today.isoformat(), "stamp": stamp, "count": n,
             "rows": rows[:25], **social_posts(rows, today, url)}
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
