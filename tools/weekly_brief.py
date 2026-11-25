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
forbid redistribution). IPOs and lock-ups: Nasdaq's calendar, whose terms forbid
redistribution too, says which companies to look at; every figure printed is
read from their SEC filings (sec_ipo.py).

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
import json
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

from weekly_brief_core import (  # noqa: E402,F401 — the shared brief; re-exported for callers
    CLUSTER_HISTORY, CONTACT_KEY, DISCLAIMER, ECONOMY_SERIES, ISSUE_DISCLAIMER, MARKET_SERIES, METHOD_URL, REGIME_SERIES,
    SITE_URL, WINDOW_DAYS,
    IPO_DAYS, brief_body, clusters_data, clusters_section, contact_from_keyring, fetch_macro_rows, fetch_macro_series,
    issue_day, issue_key, issue_url, macro_coverage, macro_events, page,
    social_posts, week_ahead,
)


class Progress:
    """What the build has done and is doing, as a file the app shows while it
    runs: the steps, and the page as far as it is known. Without a path it
    does nothing."""

    def __init__(self, path, steps):
        self.path, self.lock, self.last = path, threading.Lock(), 0.0
        self.state = {"started": time.time(), "done": False, "body": "",
                      "steps": [{"id": i, "label": label, "state": "wait", "detail": "", "part": 0, "t0": 0, "t1": 0}
                                for i, label in steps]}
        self.write(True)

    def step(self, sid, state=None, detail=None, part=None):
        with self.lock:
            s = next((x for x in self.state["steps"] if x["id"] == sid), None)
            if s is None:
                return
            changed = state is not None and state != s["state"]
            if changed:
                s["state"] = state
                s["t0" if state == "run" else "t1"] = time.time()
            if detail is not None:
                s["detail"] = detail
            if part is not None:
                s["part"] = round(part, 3)
            self.write(changed)

    def page(self, body):
        with self.lock:
            self.state["body"] = "\n".join(body)
            self.write(True)

    def finish(self):
        with self.lock:
            self.state["done"] = True
            self.write(True)

    def write(self, now=False):
        # Details come hundreds at a time (a filing each): a few writes a second.
        if not self.path or (not now and time.time() - self.last < 0.4):
            return
        self.last = time.time()
        tmp = f"{self.path}.tmp"
        try:
            Path(tmp).write_text(json.dumps(self.state, ensure_ascii=False), encoding="utf-8")
            os.replace(tmp, self.path)
        except OSError:  # a progress view never fails a build
            pass


def screener_doc(timeout=900, say=None):
    """One scan of the strategy screens, run in finterm's analytics Python
    (pandas, yfinance) — this script runs under the system Python. Returns
    (doc, error-or-None). @say(text): its progress lines."""
    import subprocess
    import tempfile

    import finterm_paths

    root = finterm_paths.data_root() / "venv-numpy2"
    py = root / ("Scripts/python.exe" if sys.platform.startswith("win") else "bin/python3")
    if not py.exists():
        return None, "finterm's analytics Python is not installed"
    with tempfile.TemporaryFile("w+", encoding="utf-8") as out:
        p = subprocess.Popen([str(py), str(SCRIPTS / "screener_data.py"), "scan", "{}"], stdin=subprocess.DEVNULL,
                             stdout=out, stderr=subprocess.PIPE, text=True, errors="replace")
        timed_out = threading.Event()
        timer = threading.Timer(timeout, lambda: (timed_out.set(), p.kill()))
        timer.start()
        try:
            for line in p.stderr:
                parts = line.rstrip().split(" ", 3)
                if say and parts[0] == "PROGRESS" and len(parts) == 4:
                    try:
                        say(parts[3], int(parts[1]) / max(1, int(parts[2])))
                    except ValueError:
                        say(parts[3])
            p.wait()
        finally:
            timer.cancel()
            p.stderr.close()
        if timed_out.is_set():
            return None, "the scan timed out"
        out.seek(0)
        text = out.read().strip()
    try:
        doc = json.loads(text.splitlines()[-1]) if text else {}
    except ValueError:
        doc = {}
    if not isinstance(doc, dict):  # a stray last line must not take the issue down
        doc = {}
    if not doc or doc.get("error"):
        return None, doc.get("error") or f"the scan failed (exit {p.returncode})"
    return doc, None


ALL_TOPICS = ("summary", "macro", "ipo", "insider", "funds", "screens")


def build_brief(today, topics=ALL_TOPICS, detail="standard", scan=True, extras=True, progress_path=None):
    """The issue's one page and what it was built from: {body, rows, notes,
    complete, series, flows, screens}. Everything is read at once, and each
    part of the page is there as soon as what it needs is. Anything that
    cannot be read is said on the page; none of it holds the issue back.
    Without @extras only the insider section, and nothing is fetched for the
    others."""
    if not extras:
        topics = [t for t in topics if t == "insider"]
    mon, fri = week_ahead(today)
    got = {"series": {}, "doc": None, "scr_err": None, "flows": None, "events": [], "macro_err": None,
           "rows": [], "notes": [], "complete": True, "ipo": None}

    def economy(say):
        got["series"] = fetch_macro_series(today)
        missing = len(MARKET_SERIES) + len(ECONOMY_SERIES) + len(REGIME_SERIES) - len(got["series"])
        return f"{missing} series not read" if missing else None

    def calendar(say):
        rows, failed = fetch_macro_rows((mon, fri), say=say)
        got["macro_err"] = macro_coverage(rows, mon, fri, failed)
        got["events"] = macro_events(rows, mon, fri)
        return got["macro_err"]

    def screens(say):
        got["doc"], got["scr_err"] = screener_doc(say=say)
        return got["scr_err"]

    def filings(say):
        if scan:  # what is already read, at once: the page fills in from it
            got["rows"], _, _ = clusters_data(False)
            progress.page(page(pending))
        got["rows"], got["notes"], got["complete"] = clusters_data(
            scan, lambda day, n, of, left: say(f"{day:%a %b %-d}: {n:,} of {of:,}", n / max(1, of)),
            waiting=lambda: say("another scan is reading; waiting for it"))
        say(f"{len(got['rows'])} companies")
        return got["notes"][0] if got["notes"] and not got["complete"] else None

    def funds(say):
        import sec_13f_bulk

        got["flows"] = sec_13f_bulk.stock_flows()
        return got["flows"].get("error")

    def ipos(say):
        got["ipo"] = ipo_week(today)
        if got["ipo"].get("error"):
            return got["ipo"]["error"]
        return "partly read" if got["ipo"].get("partial") or got["ipo"].get("ahead_unread") else None

    # step, its label, what of the page it feeds, the work
    steps = [s for s, wanted in (
        (("economy", "Economy and rates", "series", economy), {"macro", "summary"} & set(topics)),
        (("calendar", "Release calendar", "events", calendar), {"macro", "summary"} & set(topics)),
        (("screens", "Stock screens", "doc", screens), {"screens", "summary"} & set(topics)),
        (("filings", "SEC Form 4 filings", "rows", filings), True),  # also the evergreen insider page's
        (("funds", "13F fund moves", "flows", funds), "funds" in topics),
        (("ipos", "IPOs and lock-ups", "ipo", ipos), "ipo" in topics)) if wanted]
    pending = {feeds for _, _, feeds, _ in steps}
    progress = Progress(progress_path, [(sid, label) for sid, label, _, _ in steps] + [("page", "Write the page")])

    def page(pending):
        return brief_body(topics, detail, got["series"], got["doc"], got["scr_err"], got["rows"], got["notes"],
                          got["flows"], got["events"], mon, fri, got["macro_err"], today, frozenset(pending), got["ipo"])

    def run(sid, work):
        progress.step(sid, "run")
        try:
            return work(lambda text, part=None: progress.step(sid, detail=text, part=part))
        except Exception as e:  # noqa: BLE001 — one part never takes the issue with it
            why = type(e).__name__
            print(f"warning: {sid}: {why}: {e}", file=sys.stderr)
            if sid == "filings":
                got.update(complete=False, notes=[f"The SEC filings could not be read this week ({why})."])
            elif sid == "screens":
                got["scr_err"] = why
            elif sid == "funds":
                got["flows"] = {"error": why}
            elif sid == "calendar":
                got["macro_err"] = f"the release calendar could not be read ({why})"
            elif sid == "ipos":
                got["ipo"] = {"error": f"could not be read ({why})"}
            return why

    progress.page(page(pending))
    with ThreadPoolExecutor(max_workers=len(steps)) as pool:
        jobs = {pool.submit(run, sid, work): (sid, feeds) for sid, _, feeds, work in steps}
        for job in as_completed(jobs):
            sid, feeds = jobs[job]
            warn = job.result()
            if warn:
                print(f"warning: {sid}: {warn}", file=sys.stderr)
            pending.discard(feeds)
            progress.step(sid, "warn" if warn else "done", warn)
            progress.page(page(pending))
    progress.step("page", "run")
    return {"body": page(set()), **got, "screens": bool(got["doc"]), "progress": progress}


def ipo_week(today):
    """{ahead, lockups, unread}: the next seven days' IPOs and lock-up ends. The
    calendar says which companies to look at; sec_ipo reads their filings."""
    import ipo_calendar
    import sec_ipo

    data, cache = ipo_calendar.build(today=today, enrich=True), ipo_calendar.Cache()
    if not data.get("deals"):
        return {"error": "the calendar could not be read"}
    hi = today + timedelta(days=IPO_DAYS)
    lockups, lockups_unread = sec_ipo.lockups(data.get("lockups") or [], today, hi, cache)
    deals, deals_unread = sec_ipo.ahead(data["deals"], today, hi, cache)
    return {"ahead": deals, "ahead_unread": deals_unread, "lockups": lockups, "unread": lockups_unread,
            # part of the calendar, or of EDGAR, did not answer: "none" is not known
            "partial": bool(data.get("errors"))}


SECTION_WORDS = {"summary": "the market at a glance", "macro": "markets, economy and the week ahead",
                 "ipo": "IPOs and lock-ups", "insider": "insider buying", "funds": "fund moves",
                 "screens": "screen leaders"}


def write_site(site_dir, rows, notes, today, body, complete=True, topics=ALL_TOPICS, data=None):
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
    ap.add_argument("--progress", help="a file to keep the build's progress in, for the app to show")
    ap.add_argument("--no-scan", action="store_true", help="use the Form 4 data already scanned; don't fetch")
    ap.add_argument("--no-extras", action="store_true",
                    help="the insider section only (the others fetch: the screener scan takes minutes) — "
                         "e.g. for a quick offline rebuild with --no-scan")
    ap.add_argument("--allow-incomplete", action="store_true",
                    help="site mode: publish even when some days of filings could not be read")
    ap.add_argument("--topics", default=",".join(ALL_TOPICS),
                    help="comma-separated sections: summary, macro, ipo, insider, funds, screens")
    ap.add_argument("--detail", default="standard", choices=("full", "standard", "short"),
                    help="rows a table holds: 10, 5 or 3")
    ap.add_argument("--draft", action="store_true", help="accepted, without effect (a draft holds what is published)")
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

    if not os.environ.get(CONTACT_KEY):
        c = contact_from_keyring()
        if c:
            os.environ[CONTACT_KEY] = c
    if not os.environ.get(CONTACT_KEY):
        print("note: no SEC contact email found (Settings › Credentials › Contact email); "
              "SEC may refuse the requests.", file=sys.stderr)

    today = date.today()
    week_of = today - timedelta(days=today.weekday())
    if args.site:
        b = build_brief(today, topics, args.detail, scan=not args.no_scan,
                        extras=not args.no_extras, progress_path=args.progress)
        if not b["complete"] and not args.allow_incomplete:
            # A partial week published and announced reads as a quiet week.
            # Fail loudly; the next run resumes where this one stopped.
            for n in b["notes"]:
                print(f"error: {n}", file=sys.stderr)
            print("error: not publishing an incomplete issue (--allow-incomplete overrides)", file=sys.stderr)
            sys.exit(2)
        print(write_site(Path(args.site), b["rows"], b["notes"], today, b["body"], complete=b["complete"], topics=topics,
                         data=b))
        b["progress"].step("page", "done")
        b["progress"].finish()
        return

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# Weekly brief, {today:%B %-d, %Y}", ""]
    lines += build_brief(today, topics, args.detail, scan=not args.no_scan, extras=not args.no_extras)["body"]
    lines += ["---", "", f"_{ISSUE_DISCLAIMER}_", ""]
    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
